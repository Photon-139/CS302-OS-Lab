// Part III.2, machine B: S2 and S3 as two threads of one process, connected by a lock-free
// single-producer/single-consumer ring (as in Part II.1). S2 receives the smoothed strips from
// S1 on machine A over TCP; S3 sharpens and writes the output image (part3_2_A.cpp runs S1).
//
// Usage: ./part3_2_B <variant> <input.ppm> <output.ppm> <port>
//
// Start B first; it waits for A to connect. B takes k and the ring capacity from the run
// parameters A sends; it checks that A runs the same variant as B was started with, used the
// same image size and was built with the same iteration counts. When the output is written,
// B tells A ("done"), so A can measure the end-to-end time with its own clock.

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
#include "../spsc_ring.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../net_utils.hpp"
#include "../stats.hpp"

using unit = std::chrono::microseconds;

// Everything the two stage threads share. The socket channel is read only by S2; the ring has
// exactly one producer thread (S2) and one consumer thread (S3).
struct pipeline_ctx{
    const flat_image_t* orig;       // input image (B's own copy), read-only for both stages
    int k;                          // rows per strip, as received from A
    flat_image_t* output;           // final image, written only by S3
    const char* output_path;        // S3 writes the result here
    fd_channel s1_to_s2;            // from machine A (read-only: -1 as write end)
    spsc_ring s2_to_s3;             // on this machine, capacity as received from A
    long long write_us = 0;         // file write time, measured by S3 (read by main after join)

    pipeline_ctx(const flat_image_t* orig, int k, flat_image_t* output, const char* output_path,
                 int ring_capacity, int socket_fd) :
    orig(orig),
    k(k),
    output(output),
    output_path(output_path),
    s1_to_s2(socket_fd, -1, k, orig->width),
    s2_to_s3(ring_capacity, k, orig->width)
    {}
};

// S3 does the file writing (as the assignment prescribes); its duration is recorded separately.
static void write_output(pipeline_ctx* ctx)
{
    auto start = std::chrono::steady_clock::now();
    write_ppm_file_flat(ctx->output_path, ctx->output);
    auto end = std::chrono::steady_clock::now();
    ctx->write_us = std::chrono::duration_cast<unit>(end - start).count();
}

// ---------------------------------------------------------------------------
// Variant 1: every iteration of every stage is streamed strip by strip.
// ---------------------------------------------------------------------------

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

            // Receive the next smoothed strip from machine A and check it is exactly the one expected.
            strip_view input = ctx->s1_to_s2.receive();
            verify_strip(*input.hdr, input.pixels, width, iteration, r0, nrows);

            // Compute from the received strip straight into a ring slot for S3.
            strip_view output = ctx->s2_to_s3.acquire();
            S2_details(ctx->orig->image_pixels + (size_t)r0 * row_bytes, input.pixels, n, output.pixels);

            output.hdr->iteration = iteration;
            output.hdr->r0 = (uint32_t)r0;
            output.hdr->nrows = (uint32_t)nrows;
            output.hdr->flags = 0;
            output.hdr->checksum = strip_checksum(*output.hdr, output.pixels, n);

            ctx->s2_to_s3.publish();
            ctx->s1_to_s2.release();   // only now: the received pixels were read above
        }
    }
    return nullptr;
}

void* s3_variant1(void* arg){
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k = ctx->k;
    const size_t row_bytes = (size_t)width * CHANNELS;

    for (uint32_t iteration = 0; iteration < (uint32_t)config::ITERATIONS; ++iteration) {
        for (int r0 = 0; r0 < height; r0 += k) {
            const int nrows = std::min(k, height - r0);
            const size_t n = (size_t)nrows * row_bytes;
            const size_t offset = (size_t)r0 * row_bytes;

            // Receive and check the next details strip, then sharpen it into the final image.
            strip_view input = ctx->s2_to_s3.receive();
            verify_strip(*input.hdr, input.pixels, width, iteration, r0, nrows);
            S3_sharpen(ctx->orig->image_pixels + offset, input.pixels, n, ctx->output->image_pixels + offset);
            ctx->s2_to_s3.release();
        }
    }
    // The output now holds the last iteration's result.
    write_output(ctx);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Variant 2: each stage runs PASSES_PER_RESULT passes per result. Only pass 0 of
// each block communicates (S2/S3 receive, S2 sends); the other passes are
// ordinary full computations whose output stays local.
// ---------------------------------------------------------------------------

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

                // Pass 0: receive this strip of the new result from machine A and keep a copy.
                strip_view received = ctx->s1_to_s2.receive();
                verify_strip(*received.hdr, received.pixels, width, result, (uint32_t)r0, (uint32_t)nrows);
                std::memcpy(in + offset, received.pixels, n);

                // Compute straight into a ring slot for S3 and hand it on.
                strip_view sent = ctx->s2_to_s3.acquire();
                S2_details(orig_pixels + offset, in + offset, n, sent.pixels);

                sent.hdr->iteration = result;
                sent.hdr->r0        = (uint32_t)r0;
                sent.hdr->nrows     = (uint32_t)nrows;
                sent.hdr->flags     = 0;
                sent.hdr->checksum  = strip_checksum(*sent.hdr, sent.pixels, n);

                ctx->s2_to_s3.publish();
                ctx->s1_to_s2.release();   // only after the copy: the received pixels are no longer needed
            }
        }
    }

    delete[] in;
    delete[] out;
    return nullptr;
}

void* s3_variant2(void* arg){
    pipeline_ctx* ctx = static_cast<pipeline_ctx*>(arg);
    const int width  = ctx->orig->width;
    const int height = ctx->orig->height;
    const int k      = ctx->k;
    const uint32_t results = config::ITERATIONS/config::PASSES_PER_RESULT;
    const size_t row_bytes = (size_t)width * CHANNELS;
    const uint8_t* orig_pixels = ctx->orig->image_pixels;
    uint8_t* final_pixels = ctx->output->image_pixels;

    // Private buffer, allocated once for the whole run:
    //   in : the result received from S2, kept unchanged for all passes of a block
    // Every pass writes into the final image (ctx->output); the last pass of
    // the last block leaves the result that is written to file.
    uint8_t* in  = new uint8_t[(size_t)height * row_bytes];

    for(uint32_t result = 0; result<results; ++result){
        for(int pass = 0; pass<config::PASSES_PER_RESULT; ++pass){
            const bool communicate = (pass==0);

            for(int r0 = 0; r0<height; r0+=k){
                const int nrows = std::min(k, height-r0);
                const size_t n = (size_t)nrows * row_bytes;
                const size_t offset = (size_t)r0 * row_bytes;   // strip position in full-size buffers

                if(!communicate){
                    // Ordinary pass: unchanged input -> final image.
                    S3_sharpen(orig_pixels + offset, in + offset, n, final_pixels + offset);
                    continue;
                }

                // Pass 0: receive this strip of the new result and keep a copy for later passes.
                strip_view received = ctx->s2_to_s3.receive();
                verify_strip(*received.hdr, received.pixels, width, result, (uint32_t)r0, (uint32_t)nrows);
                std::memcpy(in + offset, received.pixels, n);
                ctx->s2_to_s3.release();   // the copy in `in` is all that is needed from here on

                S3_sharpen(orig_pixels + offset, in + offset, n, final_pixels + offset);
            }
        }
    }
    write_output(ctx);
    delete[] in;
    return nullptr;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv){
    if(argc != 5){
        std::cerr << "Usage: ./part3_2_B <variant> <path-to-input-image> <path-to-output-image> <port>" << std::endl;
        return 1;
    }
    const int variant = std::stoi(argv[1]);
    if(variant != 1 && variant != 2){
        std::cerr << "Unknown variant: " << variant << std::endl;
        return 1;
    }
    char* input_path        = argv[2];
    const char* output_path = argv[3];
    int port = std::stoi(argv[4]);

    // If A dies, writes fail with EPIPE ("Broken pipe" from writen) instead of killing B silently.
    signal(SIGPIPE, SIG_IGN);

    auto read_start = std::chrono::steady_clock::now();
    struct flat_image_t* image = read_ppm_file_flat(input_path);
    auto read_end = std::chrono::steady_clock::now();

    // Wait for A, then take k and the ring capacity from the parameters it sends.
    int socket_fd = listen_on(port);
    set_nodelay(socket_fd);
    run_params params = recv_params(socket_fd);     // exits on a wrong magic

    // A and B must work on the same image and with the same iteration counts (both are
    // compiled from config.hpp, so a mismatch means the two machines have different builds).
    if(params.width      != (uint32_t)image->width ||
       params.height     != (uint32_t)image->height ||
       params.iterations != (uint32_t)config::ITERATIONS ||
       params.passes     != (uint32_t)config::PASSES_PER_RESULT){
        std::cerr << "run parameters from A do not match B:"
                  << " image " << params.width << "x" << params.height
                  << " vs " << image->width << "x" << image->height
                  << ", iterations " << params.iterations << " vs " << config::ITERATIONS
                  << ", passes " << params.passes << " vs " << config::PASSES_PER_RESULT << std::endl;
        close(socket_fd);
        free_flat_image(image);
        return 1;
    }
    // Same size is not enough: both machines must have read the same pixels (S2/S3 use B's copy).
    const uint64_t image_hash = image_checksum(image);
    if(params.image_hash_hi != (uint32_t)(image_hash >> 32) ||
       params.image_hash_lo != (uint32_t)(image_hash & 0xffffffffu)){
        std::cerr << "input images differ: A and B were started with different images of the same size"
                  << std::endl;
        close(socket_fd);
        free_flat_image(image);
        return 1;
    }
    // Both machines must run the same variant (otherwise the output file name, taken from B's
    // variant, would not match the work A did).
    if(params.variant != (uint32_t)variant){
        std::cerr << "variant mismatch: A runs variant " << params.variant
                  << ", B was started with variant " << variant << std::endl;
        close(socket_fd);
        free_flat_image(image);
        return 1;
    }
    const int k = (int)params.k;
    const int ring_capacity = (int)params.ring_capacity;
    if(k < 1 || k > image->height || ring_capacity < 1){
        std::cerr << "invalid parameters from A: k=" << k
                  << " ring capacity=" << ring_capacity << std::endl;
        close(socket_fd);
        free_flat_image(image);
        return 1;
    }

    struct flat_image_t* output = new flat_image_t{image->width, image->height, nullptr};
    output->image_pixels = new uint8_t[(size_t)image->width * image->height * CHANNELS];
    pipeline_ctx context{image, k, output, output_path, ring_capacity, socket_fd};

    using stage_func = void* (*)(void*);
    stage_func funcs[2];
    if(variant==1){
        funcs[0] = s2_variant1;
        funcs[1] = s3_variant1;
    }else{
        funcs[0] = s2_variant2;
        funcs[1] = s3_variant2;
    }

    // Timed region (B's own view): from starting S2 and S3 until both have finished
    // (includes S3's file write, which is also reported separately).
    // Each stage runs through run_stage(), which prints the stage's statistics when it is done.
    const char* stage_names[2] = {"S2", "S3"};
    stage_launch launches[2];
    pthread_t threads[2];
    auto pipeline_start = std::chrono::steady_clock::now();
    const uint64_t start_ns = now_ns();
    for(int i = 0; i < 2; ++i){
        launches[i] = stage_launch{funcs[i], (void*)(&context), stage_names[i], start_ns};
        int err = pthread_create(&threads[i], NULL, run_stage, (void*)(&launches[i]));
        if(err != 0){
            std::cerr << "Failed to create thread for S" << i+2 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    for(int i = 0; i < 2; ++i){
        int err = pthread_join(threads[i], NULL);
        if(err != 0){
            std::cerr << "Failed to join thread for S" << i+2 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    auto pipeline_end = std::chrono::steady_clock::now();

    // Link statistics of this machine's side of each link (all stage threads have finished).
    print_link_stats("S1->S2", "recv", context.s1_to_s2.received(), true);
    print_link_stats("S2->S3", "send", context.s2_to_s3.sent(), false);
    print_link_stats("S2->S3", "recv", context.s2_to_s3.received(), false);

    // Output is on disk: let A stop its clock.
    send_done(socket_fd);
    close(socket_fd);

    long long read_us     = std::chrono::duration_cast<unit>(read_end - read_start).count();
    long long pipeline_us = std::chrono::duration_cast<unit>(pipeline_end - pipeline_start).count();

    // One line of key=value pairs, easy to parse in measurement scripts.
    // pipeline = B's own view (from starting S2 and S3 until the file is written);
    // the end-to-end time is the one A reports.
    std::cout << "machine=B partition=2"
              << " variant=" << variant
              << " k=" << k
              << " N=" << ring_capacity
              << " iterations=" << config::ITERATIONS
              << " read=" << read_us
              << " pipeline=" << pipeline_us
              << " write=" << context.write_us
              << "\n";

    free_flat_image(output);
    free_flat_image(image);
    return 0;
}
