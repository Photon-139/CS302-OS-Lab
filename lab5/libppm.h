#ifndef LIBPPM_H
#define LIBPPM_H
#include <cstdint>

struct image_t
{
	int width;
	int height;
	uint8_t*** image_pixels;
};

struct flat_image_t
{
	int width;
	int height;
	uint8_t* image_pixels;
};

struct image_t* read_ppm_file(char* path_to_input_file);
void write_ppm_file(char* path_to_output_file, struct image_t* image);
void free_image(struct image_t* image);

struct flat_image_t* read_ppm_file_flat(char* path_to_input_file);
void write_ppm_file_flat(char* path_to_output_file, struct flat_image_t* image);

#endif
