// Part II.3: S1, S2 and S3 as three processes, communicating through two rings in POSIX
// shared memory, synchronised with semaphores (see shm_ring).
//
// Usage: ./part2_3 <variant> <input.ppm> <output.ppm> [k] [ring_capacity]
//
// Output: the parent prints one key=value line with the overall timing; S3 prints its own
// line with the file write time (it runs in a separate process, so it reports it itself).

#include <iostream>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../libppm.hpp"
#include "../shm_ring.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../stats.hpp"

using unit = std::chrono::microseconds;

// Built once in the parent BEFORE fork(): constructing the rings maps the shared memory, and
// every stage process inherits that mapping (and a copy of this context). Each ring has
// exactly one producer and one consumer process. The rings are destroyed with the context at
// the end of main, after all stages have finished (children end with _exit, so never run it).
struct pipeline_ctx{
    const flat_image_t* orig;       // input image, read before fork, read-only for all stages
    flat_image_t* output;           // final image, written only by S3 (its own copy after fork)
    int k;                          // rows per strip
    shm_ring s1_to_s2;
    shm_ring s2_to_s3;
    const char* output_path;        // S3 writes the result here

    pipeline_ctx(const flat_image_t* orig, flat_image_t* output, int k, int ring_capacity, const char* output_path) :
    orig(orig), output(output), k(k), s1_to_s2(ring_capacity, k, orig->width),s2_to_s3(ring_capacity, k, orig->width), output_path(output_path)
    {};
};

// S3 does the file writing (as the assignment prescribes). It runs in its own process,
// so it prints the write time itself.
static void write_output(pipeline_ctx* ctx)
{
    auto start = std::chrono::steady_clock::now();
    write_ppm_file_flat(ctx->output_path, ctx->output);
    auto end = std::chrono::steady_clock::now();
    long long write_us = std::chrono::duration_cast<unit>(end - start).count();
    std::cout << "stage=S3 write=" << write_us << "\n";
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

            // Get the strip buffer and compute straight into it.
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

            // Compute from the received strip straight into the outgoing strip buffer.
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

    // Destination of the passes that are not sent on. Private to this process.
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

                // Pass 0: compute straight into the strip buffer and send it to S2.
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

                // Compute straight into the outgoing strip buffer and send it to S3.
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

// Print how a failed stage process ended.
static void report_stage(int stage, int status)
{
    if(WIFSIGNALED(status)){
        std::cerr << "S" << stage << " was killed by signal " << WTERMSIG(status)
                  << " (" << strsignal(WTERMSIG(status)) << ")" << std::endl;
    }else{
        std::cerr << "S" << stage << " exited with status " << WEXITSTATUS(status) << std::endl;
    }
}

// Wait for all started stage processes, in whatever order they finish.
//
// Semaphores give no EOF: if one stage dies, the others stay blocked in sem_wait forever.
// So as soon as one stage fails, every stage still running is terminated (SIGTERM).
//
// pids[0..started)      : the stage processes that were successfully forked (S1, S2, S3 order)
// fork_failed           : true if not all stages could be started; those that were are
//                         terminated right away, since the pipeline can never complete
// Returns true only if every stage exited normally with status 0.
static bool wait_all(const pid_t pids[], int started, bool fork_failed)
{
    bool running[3] = {false, false, false};
    for(int i = 0; i < started; ++i) running[i] = true;

    // Becomes true at the first failure. Used to (1) report and react to that failure only once,
    // and (2) ignore the stages that end afterwards because *we* terminated them.
    bool failure_seen = fork_failed;
    if(fork_failed){
        for(int i = 0; i < started; ++i) kill(pids[i], SIGTERM);
    }

    int remaining = started;
    while(remaining > 0){
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);          // whichever child finishes first
        if(pid < 0){
            if(errno == EINTR) continue;
            perror("waitpid");
            return false;
        }

        // Which stage was it?
        int stage = -1;
        for(int i = 0; i < started; ++i){
            if(pids[i] == pid) stage = i;
        }
        if(stage < 0) continue;                       // not one of our stages
        running[stage] = false;
        --remaining;

        bool stage_succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if(stage_succeeded) continue;

        if(!failure_seen){
            // The first (real) failure: report it, then stop the stages still running,
            // which would otherwise wait on the dead stage forever.
            report_stage(stage + 1, status);
            failure_seen = true;
            for(int i = 0; i < started; ++i){
                if(running[i]) kill(pids[i], SIGTERM);
            }
        }
        // Otherwise: a stage we terminated ourselves; nothing to report.
    }
    return !failure_seen;
}

int main(int argc, char** argv){
    if(argc<4 || argc>6){
        std::cerr << "Usage: ./part2_3 <variant> <path-to-input-image> <path-to-output-image> [k] [capacity]" << std::endl;
        return 1;
    }
    int variant = std::stoi(argv[1]);
    if(variant!=2 && variant!=1){
        std::cerr << "Unknown variant: " << variant << std::endl;
        return 1;
    }
    int k = argc>4 ? std::stoi(argv[4]) : config::K;
    int capacity = argc>5 ? std::stoi(argv[5]) : config::RING_CAPACITY;

    auto read_start = std::chrono::steady_clock::now();
    struct flat_image_t* image = read_ppm_file_flat(argv[2]);
    auto read_end = std::chrono::steady_clock::now();

    if(k < 1 || k > image->height){
        std::cerr << "k must be between 1 and the image height (" << image->height << "), got " << k << std::endl;
        return 1;
    }
    if(capacity < 1){
        std::cerr << "ring capacity must be at least 1, got " << capacity << std::endl;
        return 1;
    }


    struct flat_image_t* output = new flat_image_t{image->width, image->height, nullptr};
    output->image_pixels = new uint8_t[(size_t)image->width * image->height * CHANNELS];

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

    // Creates both rings in shared memory: must happen before fork().
    pipeline_ctx ctx{image, output, k, capacity, argv[3]};

    // Nothing buffered may be inherited by the children, or it would be printed several times.
    std::cout.flush();
    fflush(stdout);

    // Timed region: from starting the stages until all of them have finished
    // (includes S3's file write, which S3 also reports separately).
    pid_t pids[3];
    int started = 0;
    const char* stage_names[3] = {"S1", "S2", "S3"};
    auto pipeline_start = std::chrono::steady_clock::now();
    const uint64_t start_ns = now_ns();

    for(int i = 0; i<3; ++i){
        pid_t pid = fork();
        if(pid<0){
            perror("fork");
            break;                  // stop starting stages; wait_all terminates those already running
        }
        if(pid==0){
            // ---- child: stage i (0 = S1, 1 = S2, 2 = S3) ----
            funcs[i](&ctx);

            // This process's statistics: its stage, and its side of each link it uses.
            print_stage_stats(stage_names[i], start_ns);
            if(i==0){
                print_link_stats("S1->S2", "send", ctx.s1_to_s2.sent(), false);
            }
            if(i==1){
                print_link_stats("S1->S2", "recv", ctx.s1_to_s2.received(), false);
                print_link_stats("S2->S3", "send", ctx.s2_to_s3.sent(),     false);
            }
            if(i==2){
                print_link_stats("S2->S3", "recv", ctx.s2_to_s3.received(), false);
            }

            // _exit, not exit/return: the child must never continue into the parent's code,
            // and must not run ctx's destructor (that would destroy the rings for everyone).
            std::cout.flush();
            fflush(stdout);
            _exit(0);
        }
        // ---- parent ----
        pids[started++] = pid;
    }

    bool all_stages_succeeded = wait_all(pids, started, /*fork_failed=*/started != 3);
    auto pipeline_end = std::chrono::steady_clock::now();

    if(!all_stages_succeeded){
        std::cerr << "pipeline failed" << std::endl;
        free_flat_image(output);
        free_flat_image(image);
        return 1;
    }

    long long read_us     = std::chrono::duration_cast<unit>(read_end - read_start).count();
    long long pipeline_us = std::chrono::duration_cast<unit>(pipeline_end - pipeline_start).count();

    // One line of key=value pairs, easy to parse in measurement scripts.
    // pipeline = all three stages incl. S3's write (S3 prints write= on its own line).
    std::cout << "variant=" << variant
              << " k=" << k
              << " N=" << capacity
              << " iterations=" << config::ITERATIONS
              << " read=" << read_us
              << " pipeline=" << pipeline_us
              << "\n";

    free_flat_image(output);
    free_flat_image(image);

    return 0;
}
