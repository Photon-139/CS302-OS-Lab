#include "image_operations.hpp"
#include <algorithm>

constexpr int kernel[3][3] = {{1,1,1}, {1,1,1}, {1,1,1}};
// constexpr int kernel[9] = {1,1,1,1,1,1,1,1,1};

void S1_smooth(const flat_image_t* orig, int r0, int r1, uint8_t* __restrict out)
{
	for(int r = 0; r<(r1-r0); ++r){
		for(int c = 0; c<orig->width; ++c){
			for(int channel = 0; channel<3; ++channel){
				int sum = 0;
				for(int dr = -1; dr<2; ++dr){
					for(int dc = -1; dc<2; ++dc){
						int nei_r = std::clamp((r0+r)+dr, 0, orig->height-1);
						int nei_c = std::clamp(c+dc, 0, orig->width-1);
						sum += orig->image_pixels[pixel_index(orig->width, nei_r, nei_c, channel)]*kernel[1+dr][1+dc];
					}
				}
				out[pixel_index(orig->width, r, c, channel)] = std::clamp(sum/9, 0, 255);
			}
		}
	}
}
void S2_details(const uint8_t* __restrict orig_rows,
                const uint8_t* smooth_rows,
                size_t n, uint8_t* out)
{
	for(size_t i = 0; i<n; ++i){
		out[i] = (uint8_t)std::clamp((int)orig_rows[i]-(int)smooth_rows[i], 0, 255);
	}
}

void S3_sharpen(const uint8_t* __restrict orig_rows,
                const uint8_t* detail_rows,
                size_t n, uint8_t* out)
{
	for(size_t i = 0; i<n; ++i){
		out[i] = (uint8_t)std::clamp((int)orig_rows[i]+(int)detail_rows[i], 0, 255);
	}
}