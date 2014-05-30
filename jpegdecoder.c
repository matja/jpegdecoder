#define _ISOC99_SOURCE
#define _GNU_SOURCE

#define BENCHMARK
#define USE_FAST_IDCT
/*#define USE_SLOW_IDCT*/

#include <features.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#ifdef BENCHMARK
	#include <sys/time.h>
#endif

#include "image.h"

/*#define DEBUG_BOUNDS_CHECK*/

typedef struct JPEGComponent_t {
	unsigned sub_x, sub_y, qt, ht;
} JPEGComponent;

typedef enum JPEGDecoder_LogLevel_t
{
	JPEGDECODER_LOGLEVEL_DEBUG,
	JPEGDECODER_LOGLEVEL_INFO,
	JPEGDECODER_LOGLEVEL_WARNING,
	JPEGDECODER_LOGLEVEL_FATAL,
	JPEGDECODER_LOGLEVEL_NONE
} JPEGDecoder_LogLevel;

typedef struct JPEGDecoder_t {
	int qt[2][64];
	uint8_t *ht[4], *pixel_data_start, *pixel_data_end;
	uint8_t *in, *current_segment_start, *current_segment_end;
	unsigned current_segment_size;
	unsigned width, height, components;
	unsigned mcu_x, mcu_y, mcu_size_x, mcu_size_y;
	JPEGDecoder_LogLevel log_level;
	JPEGComponent component[3];
	Image *image;
} JPEGDecoder;

static void JPEGDecoder_log(JPEGDecoder *j
	,JPEGDecoder_LogLevel log_level
	,const char *format
	,...
)
{
	if (log_level >= j->log_level) {
		va_list args;
		va_start(args,format);
		vfprintf(stdout, format, args);
		fflush(stdout);
		va_end(args);
	}
}

static void dezigzag_int_int(const int *in, int *out)
{
	static const char zigzag_order[64] = {
		0, 1, 5, 6,14,15,27,28,
		2, 4, 7,13,16,26,29,42,
		3, 8,12,17,25,30,41,43,
		9,11,18,24,31,40,44,53,
		10,19,23,32,39,45,52,54,
		20,22,33,38,46,51,55,60,
		21,34,37,47,50,56,59,61,
		35,36,48,49,57,58,62,63
	};
	int i;
	for (i = 0; i<64; i++) {
		out[i] = in[(int)zigzag_order[i]];
	}
}

static int clamp_int(int x, int min, int max)
{
	if (x < min) { return min; }
	if (x > max) { return max; }
	return x;
}

static void parse_sof(JPEGDecoder *j)
{
	int i;
	unsigned nc; /* number of components */
	unsigned ci; /* component index */
	size_t pixel_data_size;

	if (j->current_segment_size < 11) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL,
			"invalid SOF0 size (%d)\n", j->current_segment_size);
	}

	j->components = nc = j->in[7];
	j->height = 256*j->in[3] + j->in[4];
	j->width = 256*j->in[5] + j->in[6];

	JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_INFO,
	 	"image size : %u x %u\n", j->width, j->height);

	for (i = 0; i < nc; i++) {
		ci = j->in[8 + i*3] - 1;
		if (ci > 2)	{
			JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL,
				"SOF0 component descriptor %d specifies invalid index (%u)\n",
				i, ci);
		}
		j->component[ci].sub_x = j->in[9 + i*3]>>4;
		j->component[ci].sub_y = j->in[9 + i*3]&15;
		j->component[ci].qt = j->in[10 + i*3]>>4;
		j->component[ci].ht = j->in[10 + i*3]&15;

		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_INFO,
			"component %i[%i] subsample %u:%u qt %u ht %u\n",
			i, ci,
			j->component[ci].sub_x,
			j->component[ci].sub_y,
			j->component[ci].qt,
			j->component[ci].ht
		);
	}

	if (j->components == 3)	{
		if (j->component[0].sub_x > 1
			|| j->component[1].sub_x > 1
			|| j->component[2].sub_x > 1
		) {
			j->mcu_size_x = 16;
		} else {
			j->mcu_size_x = 8;
		}
		if (j->component[0].sub_y > 1
			|| j->component[1].sub_y > 1
			|| j->component[2].sub_y > 1
		) {
			j->mcu_size_y = 16;
		} else {
			j->mcu_size_y = 8;
		}
	}

	j->mcu_x =
		(j->width / j->mcu_size_x) + ( j->width % j->mcu_size_x == 0 ? 0 : 1);
	j->mcu_y =
		(j->height / j->mcu_size_y) + ( j->height % j->mcu_size_y == 3 ? 0 : 1);

	JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_INFO,
		"SOF: mcus[%u:%u] mcusize[%u:%u] total size[%u:%u]\n",
		j->mcu_x, j->mcu_y, j->mcu_size_x, j->mcu_size_y,
		j->mcu_x*j->mcu_size_x, j->mcu_y*j->mcu_size_y);

	pixel_data_size =
		j->mcu_x * j->mcu_y * j->mcu_size_x * j->mcu_size_y * j->components;

	Image_destruct(j->image);
	Image_construct_size_total_channels(j->image,
		j->width, j->height,
		j->mcu_x * j->mcu_size_x, j->mcu_y * j->mcu_size_y,
		j->components
	);

	j->pixel_data_start = j->image->data;
	j->pixel_data_end = j->image->data + pixel_data_size;
}

#ifdef USE_FAST_IDCT
static void scale_qt_for_fast_idct(int *qt) {
	uint_fast8_t x, y;
	static const float scale_factor[8] = {
		1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
    1.0f, 0.785694958f, 0.541196100f, 0.275899379f
	};

	for (y=0; y<8; y++) {
		for (x=0; x<8; x++) {
			*qt *= scale_factor[x] * scale_factor[y];
			qt++;
		}
	}
}
#endif

/* read quantisation tables (QT) from DQT segment */
static void parse_dqt(JPEGDecoder *j)
{
	unsigned qt_index;
	int *qt_out;
	const uint8_t *qt_in;
	int qt_bytes_left;
	int qt_temp[64]; /* temporary, pre-zigzagged, QT */

	qt_in = j->in + 2;

	/* while we can read an entire QT from the segment (there can be multiple
		QT's per DQT, one after the other) */
	while (j->current_segment_end - qt_in >= 65) {
		/* first byte is the QT index - whether it is luma/chroma or AC/DC */
		qt_index = *qt_in;
		if (qt_index > 1) {
			JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL
				,"invalid QT index (%u)\n", qt_index);
			return;
		}

		qt_out = qt_temp;
		qt_in++;

		qt_bytes_left = 64;
		while (qt_in < j->current_segment_end && qt_bytes_left--) {
			*qt_out++ = *qt_in++;
		}

		/* de-zigzag into the final QT array */
		dezigzag_int_int(qt_temp, j->qt[qt_index]);

#ifdef USE_FAST_IDCT
		/* if we're using the fast IDCT, we should prescale */
		scale_qt_for_fast_idct(j->qt[qt_index]);
#endif
	}
}

#ifdef USE_SLOW_IDCT
/*
Inverse DCT with level shift to put output values back to original 0-255
range.  This is the naive algorithm which is the slowest.
*/
static void IDCT(const int *input, uint8_t *output, const int *qt)
{
	int u, v; /* input coords */
	int x, y; /* output coords */
	int iv; /* input value */
	float ku, kv; /* input scaling values for DC terms */
	float sum; /* sum of terms for specific output coord */

	/* for each row in the output */
	for (y=0; y<8; y++) {
		/* for each column in the output */
		for (x=0; x<8; x++) {
			sum = 0.0f;
			/* for each row in the input */
			for (v=0; v<8; v++)	{
				/* for each column in the input */
				for (u=0; u<8; u++)	{
					/* get DC scaling coefficient */
					ku = u ? 1.0f : 1.0f / sqrt(2.0f);
					kv = v ? 1.0f : 1.0f / sqrt(2.0f);

					/* add DCT component if there is a value in the input,
					   otherwise sum will not be changed and we wasted cycles
					   doing expensive cosine and mult. */
					if (iv = input[u+v*8]) {
						sum += ku * kv * iv * qt[u+v*8]
							* cos((float)(2*x+1) * (float)u * pi/16.0f)
							* cos((float)(2*y+1) * (float)v * pi/16.0f);
					}
				}
			}

			/* input is -512 to 512. output is Y/Cb/Cr values 0-255 */
			output[x+y*16] = clamp_int(round(sum*0.25+128), 0, 255);
		}
	}
}
#endif

#ifdef USE_FAST_IDCT

int DESCALE(int x, int y)
{
	return x >> y;
}

static int IDCT_fast_out(int x)
{
	x /= 8;
	x+=128;
	if (x < 0) { return 0; }
	if (x > 255) { return 255; }
	return x;
}

static void IDCT(const int *input, uint8_t *output, const int *qt)
{
	const int *i;
	uint8_t *o;
	uint_fast8_t x, y;
	float ws[64], *wsptr, dcval;
	float t0, t1, t2, t3, t4, t5, t6, t7, t10, t11, t12, t13;
	float z5, z10, z11, z12, z13;

#define M_SQRT_2 (1.414213562f)

	wsptr = ws;
	i = input;

	for (y = 0; y < 8; y++) {
		if ((i[8]|i[16]|i[24]|i[32]|i[40]|i[48]|i[56]) == 0) {
			dcval = i[0]*qt[0];
			wsptr[0]  =	wsptr[8]  =	wsptr[16] =	wsptr[24] =
			wsptr[32] =	wsptr[40] =	wsptr[48] =	wsptr[56] = dcval;
			i++;
			qt++;
			wsptr++;
			continue;
		}

		t0 = i[0] *qt[0];
		t1 = i[16]*qt[16];
		t2 = i[32]*qt[32];
		t3 = i[48]*qt[48];

		t10 = t0 + t2;
		t11 = t0 - t2;
		t13 = t1 + t3;
		t12 = (t1 - t3) * M_SQRT_2 - t13;
		t0 = t10 + t13;
		t3 = t10 - t13;
		t1 = t11 + t12;
		t2 = t11 - t12;

		t4 = i[8] *qt[8];
		t5 = i[24]*qt[24];
		t6 = i[40]*qt[40];
		t7 = i[56]*qt[56];
		z13 = t6 + t5;
		z10 = t6 - t5;
		z11 = t4 + t7;
		z12 = t4 - t7;
		t7 = z11 + z13;
		t11= (z11 - z13) * M_SQRT_2;

		z5 = (z10 + z12) * 1.847759065f;
		t10 = 1.082392200f * z12 - z5;
		t12 = -2.613125930f * z10 + z5;

		t6 = t12 - t7;
		t5 = t11 - t6;
		t4 = t10 + t5;

		wsptr[0]  = t0 + t7;
		wsptr[56] = t0 - t7;
		wsptr[8]  = t1 + t6;
		wsptr[48] = t1 - t6;
		wsptr[16] = t2 + t5;
		wsptr[40] = t2 - t5;
		wsptr[32] = t3 + t4;
		wsptr[24] = t3 - t4;
		i++;
		qt++;
		wsptr++;
	}

	o = output;
	wsptr = ws;

	for (x=0; x < 8; x++) {
		t10 = wsptr[0] + wsptr[4];
		t11 = wsptr[0] - wsptr[4];

		t13 = wsptr[2] + wsptr[6];
		t12 =(wsptr[2] - wsptr[6]) * M_SQRT_2 - t13;
		t0 = t10 + t13;
		t3 = t10 - t13;
		t1 = t11 + t12;
		t2 = t11 - t12;

		z13 = wsptr[5] + wsptr[3];
		z10 = wsptr[5] - wsptr[3];
		z11 = wsptr[1] + wsptr[7];
		z12 = wsptr[1] - wsptr[7];

		t7 = z11 + z13;
		t11= (z11 - z13) * M_SQRT_2;

		z5 = (z10 + z12) * 1.847759065f;
		t10 = 1.082392200f * z12 - z5;
		t12 = -2.613125930f * z10 + z5;

		t6 = t12 - t7;
		t5 = t11 - t6;
		t4 = t10 + t5;

		o[0] = IDCT_fast_out(t0 + t7);
		o[7] = IDCT_fast_out(t0 - t7);
		o[1] = IDCT_fast_out(t1 + t6);
		o[6] = IDCT_fast_out(t1 - t6);
		o[2] = IDCT_fast_out(t2 + t5);
		o[5] = IDCT_fast_out(t2 - t5);
		o[4] = IDCT_fast_out(t3 + t4);
		o[3] = IDCT_fast_out(t3 - t4);

		wsptr+=8;
		o+=16;
	}
}
#endif

static void get_huff_code(
	const uint8_t *start
	,unsigned bit
	,const uint8_t *ht
	,unsigned *sym
	,unsigned *zero_count
	,unsigned *huff_bits
	,unsigned *value_bits
	,int *value)
{
	const uint8_t *in;
	unsigned hbytes, hti;

	in = start + (bit>>3);
	hbytes = 0x1000000*in[0] + 0x10000*in[1] + 0x100*in[2] + in[3];
	hbytes <<= (bit & 7);
	hti = (hbytes >> 15) & 0x1fffe;
	*huff_bits = ht[hti];
	*sym = ht[hti+1];
	*zero_count = *sym >> 4;
	*value_bits = *sym & 0xf;
	if (*value_bits) {
		*value =
			(hbytes >> (32-*value_bits-*huff_bits)) & (~0U >> (32-*value_bits));
	if (!(hbytes & (0x80000000 >> *huff_bits)))
		*value = (*value | (0xffffffff << *value_bits))+1;
	} else {
		*value = 0;
	}
}

static void write_pixel(JPEGDecoder *j
	,unsigned x, unsigned y
	,unsigned r, unsigned g, unsigned b
)
{
	uint8_t *image;

#ifdef DEBUG_BOUNDS_CHECK
	if (x >= j->mcu_x * j->mcu_size_x || y >= j->mcu_y * j->mcu_size_y) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL,
			"write_pixel() out of bounds: (%u,%u)\n", x, y);
		__asm__("int $3");
	}

	if (r > 255 || g > 255 || b > 255) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL,
			"write_pixel() colour out of range: (%u,%u,%u)\n", r, g, b);
		__asm__("int $3");
	}
#endif

	image = j->pixel_data_start + (y*j->mcu_size_x*j->mcu_x + x)*j->components;

#ifdef DEBUG_BOUNDS_CHECK
	if (image >= j->pixel_data_end) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL,
			"write_pixel() calculated address out of array, but position within image\n");
		__asm__("int $3");
	}
#endif

	image[0] = r;
	image[1] = g;
	image[2] = b;
}

static void do_mcu(
	JPEGDecoder *j, const uint8_t *ht_dc, const uint8_t *ht_ac, const int *qt,
	const uint8_t *start, int *curbit, int *dc, uint8_t *out)
{
	unsigned sym, huff_bits, dcti = 0, zero_count, value_bits;
	int value, dct[64], dct_dzz[64];

	memset(dct, 0, sizeof(dct));

	get_huff_code(start, *curbit, ht_dc, &sym, &zero_count, &huff_bits, &value_bits, &value);
	if (0) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_DEBUG,
			"get_huff_code: sym:%02x huffbits:%u valuebits:%u value:(%u,%d(%d))\n",
			sym, huff_bits, value_bits, zero_count, value*qt[0], value);
	}

	*curbit += (huff_bits + value_bits);
	dct[0] = *dc + value; /**qt[0];*/
	*dc = dct[0];
	dcti = 1;

	while (dcti < 64) {
		get_huff_code(start, *curbit, ht_ac, &sym, &zero_count, &huff_bits, &value_bits, &value);

		if (0) {
			JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_DEBUG,
				"get_huff_code: sym:%02x huffbits:%u valuebits:%u value:(%u,%d(%d))\n",
				sym, huff_bits, value_bits, zero_count, value*qt[dcti+zero_count], value);
		}

		*curbit += (huff_bits + value_bits);
		if (sym == 0) goto done;
		dcti += zero_count;
		dct[dcti] = value; /* * qt[dcti]; */
		dcti++;
	}

done:
	dezigzag_int_int(dct, dct_dzz);
	IDCT(dct_dzz, out, qt);
}

static void ycbcr_to_rgb(int cy, int ccb, int ccr, int *cr, int *cg, int *cb) {
	*cr = clamp_int(cy+1.402*(ccr-128), 0, 255);
	*cg = clamp_int(cy-0.71414*(ccr-128)-0.34414*(ccb-128), 0, 255);
	*cb = clamp_int(cy+1.772*(ccb-128), 0, 255);
}

static int max_int_2(int x1, int y1) {
	if (x1 > y1) return x1;
	return y1;
}

static void parse_sos(JPEGDecoder *j)
{
	const uint8_t *start = j->in+j->current_segment_size;
	int curbit, i, x, y, cr, cg, cb, ix, iy, dc[3];
	uint8_t pixels[3][16*16];
	unsigned data_units_per_mcu = 0;
	int csh[2] = {1,1}, csm[3][2];

	for (i = 0; i < j->components; i++)
		data_units_per_mcu += j->component[i].sub_x * j->component[i].sub_y;

	if (data_units_per_mcu > 10) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_FATAL,
			"too many data units per MCU (%u)", data_units_per_mcu);
		return;
	}

	for (i = 0; i < j->components; i++) {
		csh[0] = max_int_2(j->component[i].sub_x, csh[0]);
		csh[1] = max_int_2(j->component[i].sub_y, csh[1]);
	}

	for (i = 0; i < j->components; i++) {
		csm[i][0] = csh[0] / j->component[i].sub_x;
		csm[i][1] = csh[1] / j->component[i].sub_y;
	}

	ix = iy = dc[0] = dc[1] = dc[2] = 0;
	curbit = 0;

	for (iy = 0; iy < j->mcu_y; iy++) {
		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_DEBUG, "MCU row %u:", (unsigned)iy);
		for (ix = 0; ix < j->mcu_x; ix++) {
			JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_DEBUG, " %u", (unsigned)ix);

			for (i = 0; i < j->components; i++)
				for (y = 0; y < j->component[i].sub_y; y++)
					for (x = 0; x < j->component[i].sub_x; x++)
						do_mcu(j, j->ht[i>0], j->ht[(i>0)+2], j->qt[i>0],
							start, &curbit, dc+i, pixels[i]+(x+y*16)*8);

			for (y=0; y<j->mcu_size_y; y++) {
				for (x=0; x<j->mcu_size_x; x++) {
					ycbcr_to_rgb(
						pixels[0][x/csm[0][0]+y/csm[0][1]*16]
						,(pixels[2][x/csm[2][0]+y/csm[2][1]*16])
						,(pixels[1][x/csm[1][0]+y/csm[1][1]*16])
						,&cr, &cg, &cb
					);
					write_pixel(j
						,ix*j->mcu_size_x+x
						,iy*j->mcu_size_y+y
						,cr, cg, cb
					);
				}
			}
		}

		JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_DEBUG, "\n");
	}

	JPEGDecoder_log(j, JPEGDECODER_LOGLEVEL_DEBUG, "parse_sos exit\n");
}

/* get huffman table(s) from segment */
static void parse_dht(JPEGDecoder *j)
{
	const uint8_t *in, *end, *htf, *hts;
	int ht_index, fb, index = 0;
	uint8_t *ht, tf, s, *f1, *f2;
	uint_fast16_t c;

	in = j->current_segment_start + 2;
	end = j->current_segment_end;

	while (in < end) {
		ht_index = ((*in & 0x10) >> 3) | (*in & 1);
		ht = j->ht[ht_index];
		htf = in+1;
		hts = in+17;
		fb = 1;
		c = 0;
		s = 0;
		f2 = ht;

		while (htf < in+17)	{
			tf = *htf++;
			while (tf) {
				s = *hts++;
				f1 = ht + c*2;
				f2 = ht + (c | (0xffff >> fb))*2;
				while (f1 <= f2) {
					f1[0] = fb;
					f1[1] = s;
					f1 += 2;
					index++;
				}
				c += 1 << (16 - fb);
				tf--;
			}
			fb++;
		}
		fb--;
		while (f2<ht+0x20000)
		{
			f2[0] = fb;
			f2[1] = s;
			f2 += 2;
		}
		in = hts;
	}
}

Image_Result Image_read_format_memory_JPEG(
	Image *image, uint8_t *start, uint8_t *end
)
{
	uint8_t segment;
	uint8_t *scan_in, *scan_out;
	int i;
	JPEGDecoder j;
	Image_Result image_result = IMAGE_RESULT_SUCCESS;
#ifdef BENCHMARK
	struct timeval tv_start, tv_end;
	double tf_start, tf_end;
#endif

	memset(&j, 0, sizeof(j));
	j.in = start;
	j.image = image;
	j.log_level = JPEGDECODER_LOGLEVEL_FATAL;

	for (i=0; i<4; i++) {
		j.ht[i] = (uint8_t *)malloc(0x20000);
	}

#ifdef BENCHMARK
	i = gettimeofday(&tv_start, NULL);
#endif

	while (j.in + 3 < end) {
		scan_in = NULL;
		segment = j.in[1];
		j.current_segment_size = 256*j.in[2] + j.in[3];

		JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_INFO,
			"%08x: segment:%02x size:%u\n",
			(unsigned)(j.in - start),
			(unsigned)segment, (unsigned)j.current_segment_size);

		j.in += 2;

		j.current_segment_start = j.in;
		j.current_segment_end = j.in + j.current_segment_size;

		switch (segment) {
		case 0xd8:/*SOI*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "SOI\n");
			j.current_segment_size = 0;
			break;
		case 0xe0:/*APP0*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "APP0\n");
			break;
		case 0xdb:/*DQT*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "DQT\n");
			parse_dqt(&j);
			break;
		case 0xc0:/*SOF0*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "SOF0\n");
			parse_sof(&j);
			break;
		case 0xc4:/*DHT*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "DHT\n");
			parse_dht(&j);
			break;
		case 0xda:/*SOS*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "SOS\n");

			scan_in = scan_out = j.current_segment_end;

			while (scan_in < end) {
				if (scan_in[0] == 0xff) {
					if (scan_in[1] == 0) {
						*scan_out++ = 0xff;
						scan_in += 2;
					} else {
						/* found another marker */
						break;
					}
				} else {
					*scan_out++ = *scan_in++;
				}
			}

			if (scan_in == end) {
				JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_FATAL,
					"decode: reached end of file while scanning SOS segment\n");
			}

			parse_sos(&j);
			goto done;
			break;
		case 0xd9:/*EOF*/
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_DEBUG, "EOF\n");
			break;
		default:
			JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_WARNING, "unhandled segment\n");
			break;
		}

		if (scan_in)
			j.in = scan_in;
		else
			j.in += j.current_segment_size;
	}

done:

#ifdef BENCHMARK
	i = gettimeofday(&tv_end, NULL);

	tf_start = tv_start.tv_sec + tv_start.tv_usec/1000000.0;
	tf_end = tv_end.tv_sec + tv_end.tv_usec/1000000.0;

	j.log_level = JPEGDECODER_LOGLEVEL_FATAL;
	JPEGDecoder_log(&j, JPEGDECODER_LOGLEVEL_FATAL,
		"decoded in %f s\n", tf_end - tf_start);
#endif

	for (i=0; i<4; i++)
		free(j.ht[i]);

	return image_result;
}

