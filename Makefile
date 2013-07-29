
CFLAGS = -O0 -ggdb -Wall -ansi -pedantic
CFLAGS = -O2 -march=nocona -mmmx -msse -ansi -pedantic -Wall

.PHONY : all clean

all : decode

clean :
	rm decode decode.o image.o jpegdecoder.o

decode : decode.o jpegdecoder.o image.o
	gcc $(CFLAGS) -o decode decode.o jpegdecoder.o image.o -lm -lSDL

image.o : image.c image.h
	gcc $(CFLAGS) -c -o image.o image.c

jpegdecoder.o : jpegdecoder.c jpegdecoder.h
	gcc $(CFLAGS) -c -o jpegdecoder.o jpegdecoder.c

decode.o : decode.c image.h
	gcc $(CFLAGS) -c -o decode.o decode.c

