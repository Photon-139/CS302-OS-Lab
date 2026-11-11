// Part II.1: S1, S2 and S3 as three threads of one process, communicating through
// lock-free single-producer/single-consumer rings synchronised with atomics.
//
// Usage: ./part2_1 <variant> <input.ppm> <output.ppm> [k] [ring_capacity]

#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <pthread.h>
#include "../libppm.hpp"
#include "../spsc_ring.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../stats.hpp"

using unit = std::chrono::microseconds;

// Everything the three stage threads share. Each ring has exactly one producer
// thread and one consumer thread.
struct pipeline_ctx{
    const flat_image_t* orig;       // input image, read-only for all stages
    flat_image_t* output;           // final image, written only by S3
    int k;                          // rows per strip
    spsc_ring s1_to_s2;
    spsc_ring s2_to_s3;
    const char* output_path;        // S3 writes the result here
    long long write_us = 0;         // file write time, measured by S3 (read by main after join)

    pipeline_ctx(const flat_image_t* orig, flat_image_t* output, int k, int ring_capacity, const char* output_path) :
    orig(orig), output(output), k(k), s1_to_s2(ring_capacity, k, orig->width), s2_to_s3(ring_capacity, k, orig->width), output_path(output_path)
    {};
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

            // Compute from the incoming slot straight into an outgoing slot.
            strip_view output = ctx->s2_to_s3.acquire();
            S2_details(ctx->orig->image_pixels + (size_t)r0 * row_bytes, input.pixels, n, output.pixels);

            output.hdr->iteration = iteration;
            output.hdr->r0 = (uint32_t)r0;
            output.hdr->nrows = (uint32_t)nrows;
            output.hdr->flags = 0;
            output.hdr->checksum = strip_checksum(*output.hdr, output.pixels, n);

            ctx->s2_to_s3.publish();
            ctx->s1_to_s2.release();   // only now: the incoming pixels were read above
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
// each block communicates (S2/S3 receive, S1/S2 send); the other passes are
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

                // Compute straight into a slot for S3 and hand it on.
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
    // Every pass writes into the shared final image (ctx->output); the last pass of
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
    if(argc<4 || argc>6){
        std::cerr << "Usage: ./part2_1 <variant> <path-to-input-image> <path-to-output-image> [k] [capacity]" << std::endl;
        return 1;
    }
    int variant = std::stoi(argv[1]);
    if(variant!=2 && variant!=1){
        std::cerr << "Unknown variant: " << variant << std::endl;
        return 1;
    }
    int k = argc>4 ? std::stoi(argv[4]) : config::K;
    int ring_capacity = argc>5 ? std::stoi(argv[5]) : config::RING_CAPACITY;

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

    struct flat_image_t* output = new flat_image_t{image->width, image->height, nullptr};
    output->image_pixels = new uint8_t[(size_t)image->width * image->height * CHANNELS];
    pipeline_ctx context{image, output, k, ring_capacity, argv[3]};

    using stage_func = void* (*)(void*);
    stage_func funcs[3];
    if(variant==1){
        funcs[0] = s1_variant1;
        funcs[1] = s2_variant1;
        funcs[2] = s3_variant1;
    }else{
        funcs[0] = s1_variant2;
        funcs[1] = s2_variant2;
        funcs[2] = s3_variant2;
    }

    // Timed region: from starting the stages until all of them have finished
    // (includes S3's file write, which is also reported separately).
    // Each stage runs through run_stage(), which prints the stage's statistics when it is done.
    const char* stage_names[3] = {"S1", "S2", "S3"};
    stage_launch launches[3];
    pthread_t threads[3];
    auto pipeline_start = std::chrono::steady_clock::now();
    const uint64_t start_ns = now_ns();
    for(int i = 0; i < 3; ++i){
        launches[i] = stage_launch{funcs[i], (void*)(&context), stage_names[i], start_ns};
        int err = pthread_create(&threads[i], NULL, run_stage, (void*)(&launches[i]));
        if(err != 0){
            std::cerr << "Failed to create thread for S" << i+1 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    for(int i = 0; i < 3; ++i){
        int err = pthread_join(threads[i], NULL);
        if(err != 0){
            std::cerr << "Failed to join thread for S" << i+1 << ": " << strerror(err) << std::endl;
            return 1;
        }
    }
    auto pipeline_end = std::chrono::steady_clock::now();

    // Link statistics (both sides live in this process; all threads have finished).
    print_link_stats("S1->S2", "send", context.s1_to_s2.sent(),     false);
    print_link_stats("S1->S2", "recv", context.s1_to_s2.received(), false);
    print_link_stats("S2->S3", "send", context.s2_to_s3.sent(),     false);
    print_link_stats("S2->S3", "recv", context.s2_to_s3.received(), false);

    long long read_us     = std::chrono::duration_cast<unit>(read_end - read_start).count();
    long long pipeline_us = std::chrono::duration_cast<unit>(pipeline_end - pipeline_start).count();

    // One line of key=value pairs, easy to parse in measurement scripts.
    // pipeline = all three stages incl. S3's write; compute = pipeline - write.
    std::cout << "variant=" << variant
              << " k=" << k
              << " N=" << ring_capacity
              << " iterations=" << config::ITERATIONS
              << " read=" << read_us
              << " pipeline=" << pipeline_us
              << " write=" << context.write_us
              << " compute=" << (pipeline_us - context.write_us)
              << "\n";

    free_flat_image(output);
    free_flat_image(image);
    return 0;
}
