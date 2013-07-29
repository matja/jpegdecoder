#include <stdio.h>
#include <stdlib.h>
#include <SDL/SDL.h>
#include "image.h"

static int max_int_2(int x1, int x2)
{
	if (x1 > x2) return x1;
	return x2;
}

static void display_image_sdl(Image *image) {
	SDL_Surface *screen;
	SDL_Event event;
	int run = 1;
	uint32_t x, y, window_size_x, window_size_y;
	uint8_t *p;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		printf("unable to initialize SDL: %s\n", SDL_GetError());
		return;
	}

	window_size_x = max_int_2(100, image->size_x);
	window_size_y = max_int_2(100, image->size_y);

	screen = SDL_SetVideoMode(window_size_x, window_size_y, 32, SDL_DOUBLEBUF);
	if (screen == NULL) {
		fprintf(stderr, "unable to set video mode: %s\n", SDL_GetError());
		return;
	}

	SDL_LockSurface(screen);

	for (y=0; y<image->size_y; y++) {
		for (x=0; x<image->size_x; x++) {
			p = ((uint8_t *)screen->pixels)+y*screen->pitch+x*screen->format->BytesPerPixel;
			p[0] = (image->data+(x*3)+(y*image->total_x*3))[0];
			p[1] = (image->data+(x*3)+(y*image->total_x*3))[1];
			p[2] = (image->data+(x*3)+(y*image->total_x*3))[2];
		}
	}

	SDL_Flip(screen);

	while (run) {

	while( SDL_PollEvent( &event ) ){
		switch( event.type ){
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
					case SDLK_ESCAPE :
						run = 0;
						break;
					default:
						break;
				}
				break;
			case SDL_KEYUP:
				break;
			default:
				break;
		}
	}
	}
	SDL_Quit();
}

int main(int argc, char *argv[])
{
	Image image;
	int exit_code = EXIT_SUCCESS;
	const char *file_name = NULL;

	if (argc == 2) {
		file_name = argv[1];
	} else {
		fprintf(stderr, "syntax: <filename>\n");
		return EXIT_FAILURE;
	}

	Image_construct(&image);
	Image_log_level_set(&image, IMAGE_LOGLEVEL_INFO);

	if (!Image_read_format_filename(&image, "JPEG", file_name)) {
		fprintf(stderr, "could not read input file: %s\n", Image_lasterror_string(&image));
		exit_code = EXIT_FAILURE;
		goto finish;
	}

	display_image_sdl(&image);

finish:
	Image_destruct(&image);

	return exit_code;
}

