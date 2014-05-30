CFLAGS = -O2 -ggdb -Wall -pedantic

.PHONY : all clean

all : decode

clean :
	@-rm -f decode decode.o image.o jpegdecoder.o

decode : decode.o jpegdecoder.o image.o
	$(CC) $(CFLAGS) -o decode decode.o jpegdecoder.o image.o -lm -lSDL

image.o : image.c image.h
	$(CC) $(CFLAGS) -c -o image.o image.c

jpegdecoder.o : jpegdecoder.c jpegdecoder.h
	$(CC) $(CFLAGS) -c -o jpegdecoder.o jpegdecoder.c

decode.o : decode.c image.h
	$(CC) $(CFLAGS) -c -o decode.o decode.c

