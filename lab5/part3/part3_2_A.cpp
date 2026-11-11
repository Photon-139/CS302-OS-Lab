// Part III.2, machine A: S1 only. Sends its smoothed strips over a TCP connection to
// machine B, which runs S2 and S3 and writes the output (part3_2_B.cpp).
//
// Usage: ./part3_2_A <variant> <input.ppm> <host-of-B> <port> [k] [ring_capacity]
//
// Start B first. A connects, sends the run parameters (B takes k, the variant and the ring
// capacity from them; the ring between S2 and S3 is on B, so A only passes the capacity on),
// runs S1, then waits until B reports that the output file has been written.
// The reported time is end-to-end, measured on A's clock only (the machines' clocks differ).

#include <iostream>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include "../libppm.hpp"
#include "../fd_channel.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../net_utils.hpp"
#include "../stats.hpp"

using unit = std::chrono::microseconds;

// Everything S1 needs. The socket channel is written only by S1.
struct pipeline_ctx{
    const flat_image_t* orig;       // input image, read-only
    int k;                          // rows per strip
    fd_channel s1_to_s2;            // to machine B (write-only: -1 as read end)

    pipeline_ctx(const flat_image_t* orig, int k, int socket_fd) :
    orig(orig),
    k(k),
    s1_to_s2(-1, socket_fd, k, orig->width)
    {}
};


// ---------------------------------------------------------------------------
// Variant 1: every iteration is streamed strip by strip.
// ---------------------------------------------------------------------------

void* s1_variant1(void* arg)
{
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width  = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k      = ctx->k;

    for (uint32_t iteration = 0; iteration < (uint32_t)config::ITERATIONS; ++iteration) {
        for (int r0 = 0; r0 < height; r0 += k) {
            const int    nrows = std::min(k, height - r0);
            const size_t n     = (size_t)nrows * width * CHANNELS;

            // Get the strip buffer and compute straight into it.
            strip_view out = ctx->s1_to_s2.acquire();
            S1_smooth(ctx->orig, r0, r0 + nrows, out.pixels);

            // Describe the strip, then seal it: the checksum is computed last.
            out.hdr->iteration = iteration;
            out.hdr->r0        = (uint32_t)r0;
            out.hdr->nrows     = (uint32_t)nrows;
            out.hdr->flags     = 0;
            out.hdr->checksum  = strip_checksum(*out.hdr, out.pixels, n);

            // Send it to S2 on machine B (blocks while the socket's buffers are full).
            ctx->s1_to_s2.publish();
        }
    }
    return nullptr;
}


// ---------------------------------------------------------------------------
// Variant 2: PASSES_PER_RESULT passes per result. Only pass 0 of each block
// sends; the other passes are ordinary full computations whose output stays local.
// ---------------------------------------------------------------------------

void* s1_variant2(void* arg){
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width  = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k      = ctx->k;

    const uint32_t results = config::ITERATIONS/config::PASSES_PER_RESULT;
    const size_t row_bytes = (size_t)width * CHANNELS;

    // Destination of the passes that are not sent on. Private to S1.
    uint8_t* scratch = new uint8_t[(size_t)height * row_bytes];

    for(uint32_t result = 0; result<results; ++result){
        for(int pass = 0; pass<config::PASSES_PER_RESULT; ++pass){
            const bool send = (pass==0);
            for(int r0 = 0; r0<height; r0+=k){
                const int nrows = std::min(k, height-r0);
                const size_t n = (size_t)nrows * row_bytes;

                if(!send){
                    // Ordinary pass: compute the strip, keep it local.
                    S1_smooth(ctx->orig, r0, r0+nrows, scratch + (size_t)r0 * row_bytes);
                    continue;
                }

                // Pass 0: compute straight into the strip buffer and send it to S2 on machine B.
                strip_view out = ctx->s1_to_s2.acquire();
                S1_smooth(ctx->orig, r0, r0 + nrows, out.pixels);

                out.hdr->iteration = result;
                out.hdr->r0        = (uint32_t)r0;
                out.hdr->nrows     = (uint32_t)nrows;
                out.hdr->flags     = 0;
                out.hdr->checksum  = strip_checksum(*out.hdr, out.pixels, n);

                ctx->s1_to_s2.publish();
            }
        }
    }
    delete[] scratch;
    return nullptr;
}


// ---------------------------------------------------------------------------

int main(int argc, char** argv){
    if(argc<5 || argc>7){
        std::cerr << "Usage: ./part3_2_A <variant> <path-to-input-image> <host-of-B> <port> [k] [capacity]" << std::endl;
        return 1;
    }
    int variant = std::stoi(argv[1]);
    if(variant!=2 && variant!=1){
        std::cerr << "Unknown variant: " << variant << std::endl;
        return 1;
    }
    const char* host = argv[3];
    int port = std::stoi(argv[4]);
    int k = argc>5 ? std::stoi(argv[5]) : config::K;
    // Capacity of the ring on machine B (between S2 and S3); A has no ring and only passes it on.
    int ring_capacity = argc>6 ? std::stoi(argv[6]) : config::RING_CAPACITY;

    // If B dies, writes fail with EPIPE ("Broken pipe" from writen) instead of killing A silently.
    signal(SIGPIPE, SIG_IGN);

    auto read_start = std::chrono::steady_clock::now();
    struct flat_image_t* image = read_ppm_file_flat(argv[2]);
    auto read_end = std::chrono::steady_clock::now();

    if(k < 1 || k > image->height){
        std::cerr << "k must be between 1 and the image height (" << image->height << "), got " << k << std::endl;
        return 1;
    }
    if(ring_capacity < 1){
        std::cerr << "ring capacity must be at least 1, got " << ring_capacity << std::endl;
        return 1;
    }

    // Checksum of the input image: B compares it with its own copy (once, before timing).
    const uint64_t image_hash = image_checksum(image);

    // Connect to B (retries while B is not listening yet) and tell it how to run.
    int socket_fd = connect_to(host, port);
    set_nodelay(socket_fd);

    run_params params{RUN_PARAMS_MAGIC,
                      (uint32_t)image->width, (uint32_t)image->height,
                      (uint32_t)k, (uint32_t)variant,
                      (uint32_t)config::ITERATIONS, (uint32_t)config::PASSES_PER_RESULT,
                      (uint32_t)ring_capacity,
                      (uint32_t)(image_hash >> 32), (uint32_t)(image_hash & 0xffffffffu)};
    send_params(socket_fd, params);

    pipeline_ctx context{image, k, socket_fd};

    using stage_func = void* (*)(void*);
    stage_func funcs[1];
    if(variant==1){
        funcs[0] = s1_variant1;
    }else{
        funcs[0] = s1_variant2;
    }

    // Timed region (end-to-end, on A's clock): from starting S1 until B reports that
    // S2 and S3 have finished and the output file is written.
    // S1 runs in its own thread, like the stages in the other parts.
    // Each stage runs through run_stage(), which prints the stage's statistics when it is done.
    const char* stage_names[1] = {"S1"};
    stage_launch launches[1];
    pthread_t threads[1];
    auto pipeline_start = std::chrono::steady_clock::now();
    const uint64_t start_ns = now_ns();
    for(int i = 0; i < 1; ++i){
        launches[i] = stage_launch{funcs[i], (void*)(&context), stage_names[i], start_ns};
        int err = pthread_create(&threads[i], NULL, run_stage, (void*)(&launches[i]));
        if(err != 0){
            std::cerr << "Failed to create thread for S" << i+1 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    for(int i = 0; i < 1; ++i){
        int err = pthread_join(threads[i], NULL);
        if(err != 0){
            std::cerr << "Failed to join thread for S" << i+1 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    wait_done(socket_fd);
    auto pipeline_end = std::chrono::steady_clock::now();

    // Link statistics of this machine's side of each link (all stage threads have finished).
    print_link_stats("S1->S2", "send", context.s1_to_s2.sent(), true);
    close(socket_fd);

    long long read_us     = std::chrono::duration_cast<unit>(read_end - read_start).count();
    long long pipeline_us = std::chrono::duration_cast<unit>(pipeline_end - pipeline_start).count();

    // One line of key=value pairs, easy to parse in measurement scripts.
    // pipeline = end-to-end time including S2, S3 and the file write on B (B reports its write time itself).
    std::cout << "machine=A partition=2"
              << " variant=" << variant
              << " k=" << k
              << " N=" << ring_capacity
              << " iterations=" << config::ITERATIONS
              << " read=" << read_us
              << " pipeline=" << pipeline_us
              << "\n";

    free_flat_image(image);
    return 0;
}
