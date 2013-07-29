#ifndef INCLUDE_IMAGE_H
#define INCLUDE_IMAGE_H

#include <stdio.h>
#include <stdint.h>

typedef struct Image_t {
	uint8_t *data;
	uint_fast32_t size_x, size_y;
	uint_fast32_t total_x, total_y;
	uint_fast8_t channels;
} Image;

typedef enum Image_LogLevel_t {
	IMAGE_LOGLEVEL_INFO,
	IMAGE_LOGLEVEL_FATAL
} Image_LogLevel;

typedef enum Image_Result_t {
	IMAGE_RESULT_FAILURE,
	IMAGE_RESULT_SUCCESS
} Image_Result;

void Image_construct(Image *this);
void Image_construct_size_channels(Image *this, uint_fast32_t size_x, uint_fast32_t size_y, uint_fast8_t channels);
void Image_construct_size_total_channels(Image *this, uint_fast32_t size_x, uint_fast32_t size_y, uint_fast32_t total_x, uint_fast32_t total_y, uint_fast8_t channels);

void Image_destruct(Image *this);

Image *Image_new(void);
void Image_delete(Image *this);

void Image_log_level_set(Image *image, Image_LogLevel log_level);
void Image_log(Image *image, Image_LogLevel log_level, const char *format, ...);
const char *Image_lasterror_string(Image *image);

void Image_convert_colourspace_YCbCr_to_RGB(const Image *in, Image *out);
void Image_convert_colourspace_RGB_to_YCbCr(const Image *in, Image *out);

Image_Result Image_read_format_memory_wrap_file(Image *image, FILE *file, Image_Result (*read_format_memory)(Image *, uint8_t *, uint8_t *));
Image_Result Image_read_format_filename(Image *image, const char *format, const char *file_name);
Image_Result Image_read_format_file_JPEG(Image *image, FILE *file);
Image_Result Image_read_format_memory_JPEG(Image *image, uint8_t *start, uint8_t *end);
Image_Result Image_write_format_filename(Image *image, const char *format, const char *file_name);

Image_Result Image_write_format_file_TGA(Image *image, FILE *file);
Image_Result Image_write_format_file_PNG(Image *image, FILE *file);

#endif 

