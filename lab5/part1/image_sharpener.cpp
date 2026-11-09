#include <iostream>
#include <cstdint>
#include <chrono>
#include "../libppm.hpp"
#include "../image_operations.hpp"

using namespace std;

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		cout << "usage: ./a.out <path-to-original-image> <path-to-transformed-image>\n\n";
		exit(0);
	}

	using unit = std::chrono::microseconds;

	auto file_read_start = std::chrono::steady_clock::now();
	struct flat_image_t *input_image = read_ppm_file_flat(argv[1]);
	auto file_read_end = std::chrono::steady_clock::now();
	auto file_read_dur = std::chrono::duration_cast<unit>(file_read_end - file_read_start);
	std::cout << "read="<< file_read_dur.count();

	// One working buffer. S1 cannot run in place (it reads neighbours), but the
	// pointwise S2 and S3 update it in place:
	//   S1: original              -> buf (smoothed)
	//   S2: original + buf        -> buf (details)
	//   S3: original + buf        -> buf (sharpened)
	const int width = input_image->width;
	const int height = input_image->height;
	const size_t num_bytes = (size_t)height * width * CHANNELS;
	struct flat_image_t *buf = new flat_image_t{width, height, new uint8_t[num_bytes]};


	auto smooth_start = std::chrono::steady_clock::now();
	S1_smooth(input_image, 0, height, buf->image_pixels);
	auto smooth_end = std::chrono::steady_clock::now();
	auto smooth_dur = std::chrono::duration_cast<unit>(smooth_end-smooth_start);
	std::cout << " s1="<< smooth_dur.count();

	auto details_start = std::chrono::steady_clock::now();
	S2_details(input_image->image_pixels, buf->image_pixels, num_bytes, buf->image_pixels);
	auto details_end = std::chrono::steady_clock::now();
	auto details_dur = std::chrono::duration_cast<unit>(details_end-details_start);
	std::cout << " s2="<<details_dur.count();

	auto sharpening_start = std::chrono::steady_clock::now();
	S3_sharpen(input_image->image_pixels, buf->image_pixels, num_bytes, buf->image_pixels);
	auto sharpening_end = std::chrono::steady_clock::now();
	auto sharpening_dur = std::chrono::duration_cast<unit>(sharpening_end - sharpening_start);
	std::cout << " s3=" << sharpening_dur.count();

	auto file_write_start = std::chrono::steady_clock::now();
	write_ppm_file_flat(argv[2], buf);
	auto file_write_end = std::chrono::steady_clock::now();
	auto file_write_dur = std::chrono::duration_cast<unit>(file_write_end-file_write_start);
	std::cout << " write=" << file_write_dur.count() << "\n";

	free_flat_image(buf);
	free_flat_image(input_image);
	return 0;
}
