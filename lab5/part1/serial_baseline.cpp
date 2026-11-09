// Serial baseline for the Part II / Part III speedup measurements (not a graded target).
//
// Does exactly the work of the pipelined versions, in one thread, with the same kernels:
//   Variant 1: ITERATIONS times  S1 -> S2 -> S3 on the whole image.
//   Variant 2: for each of ITERATIONS / PASSES_PER_RESULT results:
//                PASSES_PER_RESULT passes of S1, then of S2 (on S1's result),
//                then of S3 (on S2's result).
// So every stage runs ITERATIONS passes in both variants, as in the pipelines.
// Each pass reads unchanged input and writes a separate output buffer (no in-place updates),
// like the pipeline stages do. The output must be identical to images/reference/.
//
// Usage: ./serial_baseline <variant> <input.ppm> <output.ppm>
// Output: one key=value line; times in microseconds, s1/s2/s3 summed over all passes.

#include <iostream>
#include <chrono>
#include <string>
#include "../libppm.hpp"
#include "../image_operations.hpp"
#include "../config.hpp"

using clock_type = std::chrono::steady_clock;
using unit = std::chrono::microseconds;

static long long elapsed_us(clock_type::time_point start)
{
    return std::chrono::duration_cast<unit>(clock_type::now() - start).count();
}

int main(int argc, char** argv)
{
    if(argc != 4){
        std::cerr << "Usage: ./serial_baseline <variant> <path-to-input-image> <path-to-output-image>" << std::endl;
        return 1;
    }
    int variant = std::stoi(argv[1]);
    if(variant != 1 && variant != 2){
        std::cerr << "Unknown variant: " << variant << std::endl;
        return 1;
    }

    auto read_start = clock_type::now();
    flat_image_t* image = read_ppm_file_flat(argv[2]);
    long long read_us = elapsed_us(read_start);

    const int width  = image->width;
    const int height = image->height;
    const size_t bytes = (size_t)width * height * CHANNELS;
    const uint8_t* orig = image->image_pixels;

    // One buffer per stage output, as in the pipeline (S2/S3 never overwrite their input).
    flat_image_t* smoothed  = new flat_image_t{width, height, new uint8_t[bytes]};
    flat_image_t* details   = new flat_image_t{width, height, new uint8_t[bytes]};
    flat_image_t* sharpened = new flat_image_t{width, height, new uint8_t[bytes]};

    long long s1_us = 0, s2_us = 0, s3_us = 0;
    auto compute_start = clock_type::now();

    if(variant == 1){
        for(int iteration = 0; iteration < config::ITERATIONS; ++iteration){
            auto t = clock_type::now();
            S1_smooth(image, 0, height, smoothed->image_pixels);
            s1_us += elapsed_us(t);

            t = clock_type::now();
            S2_details(orig, smoothed->image_pixels, bytes, details->image_pixels);
            s2_us += elapsed_us(t);

            t = clock_type::now();
            S3_sharpen(orig, details->image_pixels, bytes, sharpened->image_pixels);
            s3_us += elapsed_us(t);
        }
    }else{
        const int results = config::ITERATIONS / config::PASSES_PER_RESULT;
        for(int result = 0; result < results; ++result){
            auto t = clock_type::now();
            for(int pass = 0; pass < config::PASSES_PER_RESULT; ++pass)
                S1_smooth(image, 0, height, smoothed->image_pixels);
            s1_us += elapsed_us(t);

            t = clock_type::now();
            for(int pass = 0; pass < config::PASSES_PER_RESULT; ++pass)
                S2_details(orig, smoothed->image_pixels, bytes, details->image_pixels);
            s2_us += elapsed_us(t);

            t = clock_type::now();
            for(int pass = 0; pass < config::PASSES_PER_RESULT; ++pass)
                S3_sharpen(orig, details->image_pixels, bytes, sharpened->image_pixels);
            s3_us += elapsed_us(t);
        }
    }
    long long compute_us = elapsed_us(compute_start);

    auto write_start = clock_type::now();
    write_ppm_file_flat(argv[3], sharpened);
    long long write_us = elapsed_us(write_start);

    std::cout << "mode=serial"
              << " variant=" << variant
              << " width=" << width
              << " height=" << height
              << " iterations=" << config::ITERATIONS
              << " passes=" << config::PASSES_PER_RESULT
              << " read=" << read_us
              << " s1=" << s1_us
              << " s2=" << s2_us
              << " s3=" << s3_us
              << " compute=" << compute_us
              << " write=" << write_us
              << "\n";

    free_flat_image(sharpened);
    free_flat_image(details);
    free_flat_image(smoothed);
    free_flat_image(image);
    return 0;
}
