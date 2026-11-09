// Part III.1, machine A: S1 and S2 as two threads of one process, connected by a lock-free
// single-producer/single-consumer ring (as in Part II.1). S2 sends its strips over a TCP
// connection to machine B, which runs S3 and writes the output (part3_1_B.cpp).
//
// Usage: ./part3_1_A <variant> <input.ppm> <host-of-B> <port> [k] [ring_capacity]
//
// Start B first. A connects, sends the run parameters (B takes k, the variant and the ring
// capacity from them),
// runs S1 and S2, then waits until B reports that the output file has been written.
// The reported time is end-to-end, measured on A's clock only (the machines' clocks differ).

#include <iostream>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include "../libppm.h"
#include "../spsc_ring.hpp"
#include "../fd_channel.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../net_utils.hpp"

using unit = std::chrono::microseconds;

// Everything the two stage threads share. The ring has exactly one producer thread (S1)
// and one consumer thread (S2); the socket channel is written only by S2.
struct pipeline_ctx{
    const flat_image_t* orig;       // input image, read-only for both stages
    int k;                          // rows per strip
    spsc_ring s1_to_s2;             // on this machine
    fd_channel s2_to_s3;            // to machine B (write-only: -1 as read end)

    pipeline_ctx(const flat_image_t* orig, int k, int ring_capacity, int socket_fd) :
    orig(orig),
    k(k),
    s1_to_s2(ring_capacity, k, orig->width),
    s2_to_s3(-1, socket_fd, k, orig->width)
    {}
};


// ---------------------------------------------------------------------------
// Variant 1: every iteration of every stage is streamed strip by strip.
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

            // Get a free slot (waits while the ring is full) and compute straight into it.
            strip_view out = ctx->s1_to_s2.acquire();
            S1_smooth(ctx->orig, r0, r0 + nrows, out.pixels);

            // Describe the strip, then seal it: the checksum is computed last.
            out.hdr->iteration = iteration;
            out.hdr->r0        = (uint32_t)r0;
            out.hdr->nrows     = (uint32_t)nrows;
            out.hdr->flags     = 0;
            out.hdr->checksum  = strip_checksum(*out.hdr, out.pixels, n);

            // Hand it to S2. Nothing may touch the slot after this.
            ctx->s1_to_s2.publish();
        }
    }
    return nullptr;
}

void* s2_variant1(void* arg){
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k = ctx->k;
    const size_t row_bytes = (size_t)width * CHANNELS;

    for (uint32_t iteration = 0; iteration < (uint32_t)config::ITERATIONS; ++iteration) {
        for (int r0 = 0; r0 < height; r0 += k) {
            const int nrows = std::min(k, height - r0);
            const size_t n = (size_t)nrows * row_bytes;

            // Receive the next smoothed strip and check it is exactly the one expected.
            strip_view input = ctx->s1_to_s2.receive();
            verify_strip(*input.hdr, input.pixels, width, iteration, r0, nrows);

            // Compute from the incoming slot straight into the outgoing strip buffer.
            strip_view output = ctx->s2_to_s3.acquire();
            S2_details(ctx->orig->image_pixels + (size_t)r0 * row_bytes, input.pixels, n, output.pixels);

            output.hdr->iteration = iteration;
            output.hdr->r0 = (uint32_t)r0;
            output.hdr->nrows = (uint32_t)nrows;
            output.hdr->flags = 0;
            output.hdr->checksum = strip_checksum(*output.hdr, output.pixels, n);

            ctx->s2_to_s3.publish();   // sends the strip to S3 on machine B
            ctx->s1_to_s2.release();   // only now: the incoming pixels were read above
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Variant 2: each stage runs PASSES_PER_RESULT passes per result. Only pass 0 of
// each block communicates (S2 receives, S1/S2 send); the other passes are
// ordinary full computations whose output stays local.
// ---------------------------------------------------------------------------

void* s1_variant2(void* arg){
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width  = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k      = ctx->k;

    const uint32_t results = config::ITERATIONS/config::PASSES_PER_RESULT;
    const size_t row_bytes = (size_t)width * CHANNELS;

    // Destination of the passes that are not sent on. Private to this thread.
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

                // Pass 0: compute straight into a slot and hand it to S2.
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

void* s2_variant2(void* arg){
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width  = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k      = ctx->k;
    const uint32_t results = config::ITERATIONS/config::PASSES_PER_RESULT;
    const size_t row_bytes = (size_t)width * CHANNELS;
    const uint8_t* orig_pixels = ctx->orig->image_pixels;

    // Private buffers, allocated once for the whole run:
    //   in  : the result received from S1, kept unchanged for all passes of a block
    //   out : destination of the passes that are not sent on
    uint8_t* in  = new uint8_t[(size_t)height * row_bytes];
    uint8_t* out = new uint8_t[(size_t)height * row_bytes];

    for(uint32_t result = 0; result<results; ++result){
        for(int pass = 0; pass<config::PASSES_PER_RESULT; ++pass){
            const bool communicate = (pass==0);

            for(int r0 = 0; r0<height; r0+=k){
                const int nrows = std::min(k, height-r0);
                const size_t n = (size_t)nrows * row_bytes;
                const size_t offset = (size_t)r0 * row_bytes;   // strip position in full-size buffers

                if(!communicate){
                    // Ordinary pass: unchanged input -> private output, nothing sent.
                    S2_details(orig_pixels + offset, in + offset, n, out + offset);
                    continue;
                }

                // Pass 0: receive this strip of the new result and keep a copy for later passes.
                strip_view received = ctx->s1_to_s2.receive();
                verify_strip(*received.hdr, received.pixels, width, result, (uint32_t)r0, (uint32_t)nrows);
                std::memcpy(in + offset, received.pixels, n);

                // Compute straight into the outgoing strip buffer and send it to S3 on machine B.
                strip_view sent = ctx->s2_to_s3.acquire();
                S2_details(orig_pixels + offset, in + offset, n, sent.pixels);

                sent.hdr->iteration = result;
                sent.hdr->r0        = (uint32_t)r0;
                sent.hdr->nrows     = (uint32_t)nrows;
                sent.hdr->flags     = 0;
                sent.hdr->checksum  = strip_checksum(*sent.hdr, sent.pixels, n);

                ctx->s2_to_s3.publish();
                ctx->s1_to_s2.release();   // only after the copy: the slot's pixels are no longer needed
            }
        }
    }

    delete[] in;
    delete[] out;
    return nullptr;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv){
    if(argc<5 || argc>7){
        std::cerr << "Usage: ./part3_1_A <variant> <path-to-input-image> <host-of-B> <port> [k] [capacity]" << std::endl;
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

    // Connect to B (retries while B is not listening yet) and tell it how to run.
    int socket_fd = connect_to(host, port);
    set_nodelay(socket_fd);

    run_params params{RUN_PARAMS_MAGIC,
                      (uint32_t)image->width, (uint32_t)image->height,
                      (uint32_t)k, (uint32_t)variant,
                      (uint32_t)config::ITERATIONS, (uint32_t)config::PASSES_PER_RESULT,
                      (uint32_t)ring_capacity};
    send_params(socket_fd, params);

    pipeline_ctx context{image, k, ring_capacity, socket_fd};

    using stage_func = void* (*)(void*);
    stage_func funcs[2];
    if(variant==1){
        funcs[0] = s1_variant1;
        funcs[1] = s2_variant1;
    }else{
        funcs[0] = s1_variant2;
        funcs[1] = s2_variant2;
    }

    // Timed region (end-to-end, on A's clock): from starting S1 and S2 until B reports that
    // S3 has finished and the output file is written.
    pthread_t threads[2];
    auto pipeline_start = std::chrono::steady_clock::now();
    for(int i = 0; i < 2; ++i){
        int err = pthread_create(&threads[i], NULL, funcs[i], (void*)(&context));
        if(err != 0){
            std::cerr << "Failed to create thread for S" << i+1 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    for(int i = 0; i < 2; ++i){
        int err = pthread_join(threads[i], NULL);
        if(err != 0){
            std::cerr << "Failed to join thread for S" << i+1 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    wait_done(socket_fd);
    auto pipeline_end = std::chrono::steady_clock::now();
    close(socket_fd);

    long long read_us     = std::chrono::duration_cast<unit>(read_end - read_start).count();
    long long pipeline_us = std::chrono::duration_cast<unit>(pipeline_end - pipeline_start).count();

    // One line of key=value pairs, easy to parse in measurement scripts.
    // pipeline = end-to-end time including S3 and its file write on B (B reports its write time itself).
    std::cout << "machine=A partition=1"
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
