#include <iostream>
#include "libppm.h"
#include <cstdint>
#include <algorithm>
#include <chrono>

using namespace std;

void free_image(struct image_t *image){
	if(image==nullptr || image->image_pixels==nullptr){
		return;
	}
	for(int i = 0; i<image->height; ++i){
		for(int j = 0; j<image->width; ++j){
			delete[] image->image_pixels[i][j];
		}
		delete[] image->image_pixels[i];
	}
	delete[] image->image_pixels;
	delete image;
}

struct image_t* S1_smoothen(struct image_t *input_image)
{
	// TODO
	// remember to allocate space for smoothened_image. See read_ppm_file() in libppm.c for some help.
	image_t* res = new image_t{input_image->width, input_image->height, nullptr};
	res->image_pixels = new uint8_t**[input_image->height];
	for(int i = 0; i<input_image->height; ++i){
		res->image_pixels[i] = new uint8_t*[input_image->width];
		for(int j = 0; j<input_image->width; ++j){
			res->image_pixels[i][j] = new uint8_t[3];
		}
	}
	int kernel[3][3] = {{1,1,1}, {1,1,1}, {1,1,1}};
	for(int r = 0; r<input_image->height; ++r){
		for(int c = 0; c<input_image->width; ++c){
			for(int channel = 0; channel<3; ++channel){
				int sum = 0;
				for(int dr = -1; dr<2; ++dr){
					for(int dc = -1; dc<2; ++dc){
						int nei_r = std::clamp(r+dr, 0, input_image->height-1);
						int nei_c = std::clamp(c+dc, 0, input_image->width-1);
						sum += input_image->image_pixels[nei_r][nei_c][channel]*kernel[1+dr][1+dc];
					}
				}
				res->image_pixels[r][c][channel] = std::clamp(sum/9, 0, 255);
			}
		}
	}

	return res;
}

struct image_t* S2_find_details(struct image_t *input_image, struct image_t *smoothened_image)
{
	// TODO

	for(int r = 0; r<input_image->height; ++r){
		for(int c = 0; c<input_image->width; ++c){
			for(int channel = 0; channel<3; ++channel){
				smoothened_image->image_pixels[r][c][channel] = (uint8_t)std::clamp((int)input_image->image_pixels[r][c][channel]-(int)smoothened_image->image_pixels[r][c][channel], 0, 255);
			}
		}
	}
	return smoothened_image;
}

struct image_t* S3_sharpen(struct image_t *input_image, struct image_t *details_image)
{
	for(int r = 0; r<input_image->height; ++r){
		for(int c = 0; c<input_image->width; ++c){
			for(int channel = 0; channel<3; ++channel){
				details_image->image_pixels[r][c][channel] = (uint8_t)std::clamp((int)input_image->image_pixels[r][c][channel]+(int)details_image->image_pixels[r][c][channel], 0, 255);
			}
		}
	}
	return details_image;
}

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		cout << "usage: ./a.out <path-to-original-image> <path-to-transformed-image>\n\n";
		exit(0);
	}

	using unit = std::chrono::microseconds;
	
	auto file_read_start = std::chrono::steady_clock::now();
	struct image_t *input_image = read_ppm_file(argv[1]);
	auto file_read_end = std::chrono::steady_clock::now();
	auto file_read_dur = std::chrono::duration_cast<unit>(file_read_end - file_read_start);
	std::cout << "read="<< file_read_dur.count();


	auto smooth_start = std::chrono::steady_clock::now();
	struct image_t *smoothened_image = S1_smoothen(input_image);
	auto smooth_end = std::chrono::steady_clock::now();
	auto smooth_dur = std::chrono::duration_cast<unit>(smooth_end-smooth_start);
	std::cout << " s1="<< smooth_dur.count();


	auto details_start = std::chrono::steady_clock::now();
	struct image_t *details_image = S2_find_details(input_image, smoothened_image);
	auto details_end = std::chrono::steady_clock::now();
	auto details_dur = std::chrono::duration_cast<unit>(details_end-details_start);
	std::cout << " s2="<<details_dur.count();


	auto shaprpening_start = std::chrono::steady_clock::now();
	struct image_t *sharpened_image = S3_sharpen(input_image, details_image);
	auto shaprpening_end = std::chrono::steady_clock::now();
	auto sharpening_dur = std::chrono::duration_cast<unit>(shaprpening_end - shaprpening_start);
	std::cout << " s3=" << sharpening_dur.count();

	
	auto file_write_start = std::chrono::steady_clock::now();
	write_ppm_file(argv[2], sharpened_image);
	auto file_write_end = std::chrono::steady_clock::now();
	auto file_write_dur = std::chrono::duration_cast<unit>(file_write_end-file_write_start);
	std::cout << " write=" << file_write_dur.count() << "\n";
	
	free_image(sharpened_image);
	free_image(input_image);
	return 0;
}
