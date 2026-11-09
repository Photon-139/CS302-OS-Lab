#pragma once
#include "libppm.hpp"
#include <cstddef>

void S1_smooth(const flat_image_t* orig, int r0, int r1, uint8_t* __restrict out);
// S2 and S3 are pointwise over n bytes. out may be exactly the same pointer as
// smooth_rows / detail_rows (in-place update), but must not partially overlap it.
// out must never alias orig_rows.
void S2_details(const uint8_t* __restrict orig_rows,
                const uint8_t* smooth_rows,
                size_t n, uint8_t* out);

void S3_sharpen(const uint8_t* __restrict orig_rows,
                const uint8_t* detail_rows,
                size_t n, uint8_t* out);