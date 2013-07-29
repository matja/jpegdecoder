#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <strings.h>

#include "image.h"

void Image_construct(Image *this)
{
	this->size_x = this->size_y = 0;
	this->data = 0;
}

void Image_construct_size_channels(
	Image *this,
	uint_fast32_t size_x, uint_fast32_t size_y,
	uint_fast8_t channels)
{
	size_t bytes;

	memset(this, 0, sizeof *this);
	bytes = size_x * size_y * channels;
	this->data = (uint8_t *)malloc(bytes);

	if (!this->data) {
		Image_log(this, IMAGE_LOGLEVEL_FATAL, "cannot allocate memory\n");
		return;
	}

	this->size_x = size_x;
	this->size_y = size_y;
	this->total_x = size_x;
	this->total_y = size_y;
	this->channels = channels;
}

void Image_construct_size_total_channels(
	Image *this,
	uint_fast32_t size_x, uint_fast32_t size_y,
	uint_fast32_t total_x, uint_fast32_t total_y,
	uint_fast8_t channels)
{
	size_t bytes;

	memset(this, 0, sizeof *this);

	if (size_x > total_x || size_y > total_y) {
		Image_log(this, IMAGE_LOGLEVEL_FATAL, "can't create an image size greater than total pixel size\n");
	}

	bytes = total_x * total_y * channels;
	this->data = (uint8_t *)malloc(bytes);

	if (!this->data) {
		Image_log(this, IMAGE_LOGLEVEL_FATAL, "cannot allocate memory\n");
		return;
	}

	this->size_x = size_x;
	this->size_y = size_y;
	this->total_x = total_x;
	this->total_y = total_y;
	this->channels = channels;
}

void Image_destruct(Image *this)
{
	if (this->data)
		free(this->data);
}

Image_Result Image_write_format_filename(
	Image *image,
	const char *format,
	const char *filename)
{
	FILE *output_file;
	const char *error_string;
	Image_Result image_result = IMAGE_RESULT_SUCCESS;

	output_file = fopen(filename, "wb");
	if (!output_file) {
		error_string = strerror(errno);
		Image_log(image, IMAGE_LOGLEVEL_FATAL,
			"cannot open '%s' for writing (reason: '%s'",
			filename, error_string);
		return IMAGE_RESULT_FAILURE;
	}

	if (strcasecmp(format, "TGA") == 0) {
		Image_write_format_file_TGA(image, output_file);
	} else {
		Image_log(image, IMAGE_LOGLEVEL_FATAL, "could not find appropriate output handler\n");
		image_result = IMAGE_RESULT_FAILURE;
	}

	fclose(output_file);
	return image_result;
}

Image_Result Image_write_format_file_TGA(Image *this, FILE *file)
{
	uint8_t header[18] = { 0 };
	uint_fast16_t row;

	header[2] = 2; /* image type */
	header[7] = 32; /* colour map bits (we have no colour map but some apps
		require this field to be set) */
	header[12] = this->size_x % 256;
	header[13] = this->size_x / 256;
	header[14] = this->size_y % 256;
	header[15] = this->size_y / 256;
	header[16] = 24; /* bits per plane */
	header[17] = 32; /* vflip flag : first row is top row */

  fwrite(header, sizeof header, 1, file);

  for (row = 0; row < this->size_y; row++)
  {
		fwrite(this->data + row*this->total_x * this->channels,
			this->size_x * this->channels, 1, file);
  }

	return IMAGE_RESULT_SUCCESS;
}

extern int fileno(FILE *);

Image_Result Image_read_format_memory_wrap_file(
	Image *image,
	FILE *file,
	Image_Result (*read_format_memory)(Image *, uint8_t *, uint8_t *))
{
	struct stat file_stat;
	size_t file_size;
	uint8_t *file_data;

	fstat(fileno(file), &file_stat);
	file_size = file_stat.st_size;
	file_data = (uint8_t *)malloc(file_size);
	fread(file_data, file_size, 1, file);

	read_format_memory(image, file_data, file_data + file_size);

	return IMAGE_RESULT_SUCCESS;
}

Image_Result Image_read_format_file_JPEG(
  Image *this,
	FILE *file)
{
	return Image_read_format_memory_wrap_file(this, file, Image_read_format_memory_JPEG);
}

void Image_log_level_set(Image *image, Image_LogLevel log_level) {

}

Image_Result Image_read_format_filename(
  Image *image,
	const char *format,
	const char *file_name)
{
	FILE *input_file;
	const char *error_string;
	Image_Result image_result;

	input_file = fopen(file_name, "rb");
	if (!input_file) {
		error_string = strerror(errno);

		Image_log(image, IMAGE_LOGLEVEL_FATAL,
			"cannot open '%s' for writing (reason: '%s'",
			file_name, error_string);
	}

	if (strcasecmp(format, "JPEG") == 0) {
		image_result = Image_read_format_file_JPEG(image, input_file);
	} else {
		Image_log(image, IMAGE_LOGLEVEL_FATAL, "cannot find appropriate format handler\n");
	}

	fclose(input_file);
	return image_result;
}

const char *Image_lasterror_string(Image *image)
{
	return "unknown error";
}

void Image_log(
	Image *image,
	Image_LogLevel log_level,
	const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

