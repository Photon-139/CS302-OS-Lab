// Part III.1, machine B: S3. Receives the details strips from S2 on machine A over TCP,
// sharpens them and writes the output image (part3_1_A.cpp runs S1 and S2).
//
// Usage: ./part3_1_B <input.ppm> <output.ppm> <port>
//
// Start B first; it waits for A to connect. B takes k and the variant from the run parameters
// A sends, so the two machines cannot disagree; it only checks that A used the same image size
// and was built with the same iteration counts. When the output is written, B tells A ("done"),
// so A can measure the end-to-end time with its own clock.

#include <iostream>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include "../libppm.h"
#include "../fd_channel.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../net_utils.hpp"

using unit = std::chrono::microseconds;

// Everything S3 needs. The socket channel is read only by S3.
struct pipeline_ctx{
    const flat_image_t* orig;       // input image (B's own copy), read-only
    int k;                          // rows per strip, as received from A
    flat_image_t* output;           // final image, written by S3
    const char* output_path;        // S3 writes the result here
    fd_channel s2_to_s3;            // from machine A (read-only: -1 as write end)
    long long write_us = 0;         // file write time, measured by S3

    pipeline_ctx(const flat_image_t* orig, int k, flat_image_t* output, const char* output_path, int socket_fd) :
    orig(orig),
    k(k),
    output(output),
    output_path(output_path),
    s2_to_s3(socket_fd, -1, k, orig->width)
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
// Variant 1: every iteration is streamed strip by strip.
// ---------------------------------------------------------------------------

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
// Variant 2: PASSES_PER_RESULT passes per result. Only pass 0 of each block
// receives; the other passes are ordinary full computations.
// ---------------------------------------------------------------------------

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
    if(argc != 4){
        std::cerr << "Usage: ./part3_1_B <path-to-input-image> <path-to-output-image> <port>" << std::endl;
        return 1;
    }
    char* input_path        = argv[1];
    const char* output_path = argv[2];
    int port = std::stoi(argv[3]);

    // If A dies, writes fail with EPIPE ("Broken pipe" from writen) instead of killing B silently.
    signal(SIGPIPE, SIG_IGN);

    auto read_start = std::chrono::steady_clock::now();
    struct flat_image_t* image = read_ppm_file_flat(input_path);
    auto read_end = std::chrono::steady_clock::now();

    // Wait for A, then take k and the variant from the parameters it sends.
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
    const int k = (int)params.k;
    const int variant = (int)params.variant;
    if(k < 1 || k > image->height || (variant != 1 && variant != 2)){
        std::cerr << "invalid parameters from A: k=" << k << " variant=" << variant << std::endl;
        close(socket_fd);
        free_flat_image(image);
        return 1;
    }

    struct flat_image_t* output = new flat_image_t{image->width, image->height, nullptr};
    output->image_pixels = new uint8_t[(size_t)image->width * image->height * CHANNELS];
    pipeline_ctx context{image, k, output, output_path, socket_fd};

    using stage_func = void* (*)(void*);
    stage_func funcs[1];
    if(variant==1){
        funcs[0] = s3_variant1;
    }else{
        funcs[0] = s3_variant2;
    }

    // S3 runs in its own thread, like the stages in the other parts.
    pthread_t threads[1];
    auto pipeline_start = std::chrono::steady_clock::now();
    for(int i = 0; i < 1; ++i){
        int err = pthread_create(&threads[i], NULL, funcs[i], (void*)(&context));
        if(err != 0){
            std::cerr << "Failed to create thread for S3: " << strerror(err) << std::endl;
            return 1;
        }
    }
    for(int i = 0; i < 1; ++i){
        int err = pthread_join(threads[i], NULL);
        if(err != 0){
            std::cerr << "Failed to join thread for S3: " << strerror(err) << std::endl;
            return 1;
        }
    }
    auto pipeline_end = std::chrono::steady_clock::now();

    // Output is on disk: let A stop its clock.
    send_done(socket_fd);
    close(socket_fd);

    long long read_us     = std::chrono::duration_cast<unit>(read_end - read_start).count();
    long long pipeline_us = std::chrono::duration_cast<unit>(pipeline_end - pipeline_start).count();

    // One line of key=value pairs, easy to parse in measurement scripts.
    // pipeline = B's own view (from waiting for the first strip until the file is written);
    // the end-to-end time is the one A reports.
    std::cout << "machine=B partition=1"
              << " variant=" << variant
              << " k=" << k
              << " N=" << params.ring_capacity     // A's ring; B has none in III.1
              << " iterations=" << config::ITERATIONS
              << " read=" << read_us
              << " pipeline=" << pipeline_us
              << " write=" << context.write_us
              << "\n";

    free_flat_image(output);
    free_flat_image(image);
    return 0;
}
