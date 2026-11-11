// Part II.2: S1, S2 and S3 as three processes, communicating through two pipes.
//
// Usage: ./part2_2 <variant> <input.ppm> <output.ppm> [k]
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
#include "../fd_channel.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"
#include "../stats.hpp"

using unit = std::chrono::microseconds;

// Per-process context: each stage process builds its own after fork(), holding only the
// pipe ends it uses (-1 for the others). Each pipe has exactly one writer and one reader process.
struct pipeline_ctx{
    const flat_image_t* orig;       // input image, read before fork, read-only for all stages
    flat_image_t* output;           // final image, written only by S3 (its own copy after fork)
    int k;                          // rows per strip
    fd_channel s1_to_s2;
    fd_channel s2_to_s3;
    const char* output_path;        // S3 writes the result here

    pipeline_ctx(const flat_image_t* orig, flat_image_t* output, int k, const char* output_path,
             int s1_to_s2_read, int s1_to_s2_write,
             int s2_to_s3_read, int s2_to_s3_write) :
    orig(orig), output(output), k(k), s1_to_s2(s1_to_s2_read, s1_to_s2_write, k, orig->width),s2_to_s3(s2_to_s3_read, s2_to_s3_write, k, orig->width), output_path(output_path)
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

            // Send it to S2 (blocks while the pipe is full).
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

// Wait for one stage process; true if it exited normally with status 0.
static bool wait_stage(pid_t pid, int stage)
{
    int status = 0;
    while(waitpid(pid, &status, 0) < 0){
        if(errno == EINTR) continue;
        perror("waitpid");
        return false;
    }
    if(WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    if(WIFSIGNALED(status)){
        std::cerr << "S" << stage << " was killed by signal " << WTERMSIG(status)
                  << " (" << strsignal(WTERMSIG(status)) << ")" << std::endl;
    }else{
        std::cerr << "S" << stage << " exited with status " << WEXITSTATUS(status) << std::endl;
    }
    return false;
}

int main(int argc, char** argv){
    if(argc<4 || argc>5){
        std::cerr << "Usage: ./part2_2 <variant> <path-to-input-image> <path-to-output-image> [k]" << std::endl;
        return 1;
    }
    int variant = std::stoi(argv[1]);
    if(variant!=2 && variant!=1){
        std::cerr << "Unknown variant: " << variant << std::endl;
        return 1;
    }
    int k = argc>4 ? std::stoi(argv[4]) : config::K;

    // A writer whose reader has died gets EPIPE from write() (writen reports it and aborts)
    // instead of being killed silently by SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    auto read_start = std::chrono::steady_clock::now();
    struct flat_image_t* image = read_ppm_file_flat(argv[2]);
    auto read_end = std::chrono::steady_clock::now();

    if(k < 1 || k > image->height){
        std::cerr << "k must be between 1 and the image height (" << image->height << "), got " << k << std::endl;
        return 1;
    }
    

    struct flat_image_t* output = new flat_image_t{image->width, image->height, nullptr};
    output->image_pixels = new uint8_t[(size_t)image->width * image->height * CHANNELS];

    // Pipes are created before forking so that all stage processes inherit them.
    int s1_to_s2[2];
    int s2_to_s3[2];

    if(pipe(s1_to_s2)==-1){
        perror("pipe (S1 -> S2)");
        return 1;
    }

    if(pipe(s2_to_s3)==-1){
        perror("pipe (S2 -> S3)");
        close(s1_to_s2[0]);
        close(s1_to_s2[1]);
        return 1;
    }

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
            break;                  // stop starting stages; clean up below
        }
        if(pid==0){
            // ---- child: stage i (0 = S1, 1 = S2, 2 = S3) ----
            // Keep only the ends this stage uses, close the rest.
            if(i==0){
                close(s1_to_s2[0]);
                close(s2_to_s3[0]);
                close(s2_to_s3[1]);
            }
            if(i==1){
                close(s1_to_s2[1]);
                close(s2_to_s3[0]);
            }
            if(i==2){
                close(s1_to_s2[0]);
                close(s1_to_s2[1]);
                close(s2_to_s3[1]);
            }

            // This stage's own context: the ends it kept, -1 for the closed ones.
            int s1_to_s2_read  = (i==1) ? s1_to_s2[0] : -1;
            int s1_to_s2_write = (i==0) ? s1_to_s2[1] : -1;
            int s2_to_s3_read  = (i==2) ? s2_to_s3[0] : -1;
            int s2_to_s3_write = (i==1) ? s2_to_s3[1] : -1;
            pipeline_ctx ctx(image, output, k, argv[3],
                             s1_to_s2_read, s1_to_s2_write, s2_to_s3_read, s2_to_s3_write);

            funcs[i](&ctx);

            // This process's statistics: its stage, and its side of each link it uses.
            print_stage_stats(stage_names[i], start_ns);
            if(i==0){
                print_link_stats("S1->S2", "send", ctx.s1_to_s2.sent(), true);
            }
            if(i==1){
                print_link_stats("S1->S2", "recv", ctx.s1_to_s2.received(), true);
                print_link_stats("S2->S3", "send", ctx.s2_to_s3.sent(),     true);
            }
            if(i==2){
                print_link_stats("S2->S3", "recv", ctx.s2_to_s3.received(), true);
            }

            // _exit, not exit/return: the child must never continue into the parent's code.
            std::cout.flush();
            fflush(stdout);
            _exit(0);
        }
        // ---- parent ----
        pids[started++] = pid;
    }

    // The parent uses no pipe ends. Closing them is required: a reader only sees EOF
    // (and a writer only gets EPIPE) once every copy of the other end is closed.
    close(s1_to_s2[0]);
    close(s1_to_s2[1]);
    close(s2_to_s3[0]);
    close(s2_to_s3[1]);

    bool ok = (started == 3);
    for(int i = 0; i<started; ++i){
        if(!wait_stage(pids[i], i+1)) ok = false;
    }
    auto pipeline_end = std::chrono::steady_clock::now();

    if(!ok){
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
              << " iterations=" << config::ITERATIONS
              << " read=" << read_us
              << " pipeline=" << pipeline_us
              << "\n";

    free_flat_image(output);
    free_flat_image(image);

    return 0;
}
