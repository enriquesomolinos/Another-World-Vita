
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bitmap.h"
#include "cinepak.h"
#include "opera.h"
#include "util.h"
#include "wave.h"

static const char *OUT = "out";

static uint8_t clipU8(int a) {
	if (a < 0) {
		return 0;
	} else if (a > 255) {
		return 255;
	} else {
		return a;
	}
}

static uint16_t yuv_to_rgb555(int y, int u, int v) {
	int r = int(y + (1.370705 * (v - 128)));
	r = clipU8(r) >> 3;
	int g = int(y - (0.698001 * (v - 128)) - (0.337633 * (u - 128)));
	g = clipU8(g) >> 3;
	int b = int(y + (1.732446 * (u - 128)));
	b = clipU8(b) >> 3;
	return (r << 10) | (g << 5) | b;
}

static void uyvy_to_rgb555(const uint8_t *in, int len, uint16_t *out) {
	assert((len & 3) == 0);
	for (int i = 0; i < len; i += 4, in += 4) {
		const int u  = in[0];
		const int y0 = in[1];
		const int v  = in[2];
		const int y1 = in[3];
		*out++ = yuv_to_rgb555(y0, u, v);
		*out++ = yuv_to_rgb555(y1, u, v);
	}
}

static uint16_t _cineBitmap555[320 * 100];

struct OutputBuffer {
	void setup(int w, int h, CinepakDecoder *decoder) {
		_bufSize = w * h * 2;
		_buf = (uint8_t *)malloc(_bufSize);
		decoder->_yuvFrame = _buf;
		decoder->_yuvW = w;
		decoder->_yuvH = h;
		decoder->_yuvPitch = w * 2;
	}
	void dump(const char *path, int num) {
		char name[MAXPATHLEN];
		if (1) {
			static const int W = 320;
			static const int H = 100;
			snprintf(name, sizeof(name), "%s/%04d.bmp", path, num);
			FILE *fp = fopen(name, "wb");
			if (fp) {
				const int size = W * H * sizeof(uint16_t);
				uyvy_to_rgb555(_buf, size, _cineBitmap555);
				writeBitmap555(fp, _cineBitmap555, W, H);
				fclose(fp);
			}
			return;
		}
		snprintf(name, sizeof(name), "%s/%04d.uyvy", path, num);
		FILE *fp = fopen(name, "wb");
		if (fp) {
			fwrite(_buf, _bufSize, 1, fp);
			fclose(fp);
			char cmd[256];
			snprintf(cmd, sizeof(cmd), "convert -size 320x100 -depth 8 %s/%04d.uyvy %s/%04d.png", path, num, path, num);
			system(cmd);
		}
	}

	uint8_t *_buf;
	uint32_t _bufSize;
};

static uint32_t readInt(FILE *fp) {
	uint8_t buf[4];
	fread(buf, sizeof(buf), 1, fp);
	return readUint32BE(buf);
}

static uint32_t readTag(FILE *fp, char *type) {
	fread(type, 4, 1, fp);
	uint8_t buf[4];
	fread(buf, sizeof(buf), 1, fp);
	return readUint32BE(buf);
}

static void decodeCine(FILE *fp, const char *name) {
	char path[MAXPATHLEN];
	snprintf(path, sizeof(path), "%s/%s", OUT, name);
	stringLower(path + strlen(OUT) + 1);
	makeDirectory(path);
	CinepakDecoder decoder;
	OutputBuffer out;
	int frmeCounter = 0;
	while (1) {
		const uint32_t pos = ftell(fp);
		char tag[4];
		const uint32_t size = readTag(fp, tag);
		if (feof(fp)) {
			break;
		}
		if (memcmp(tag, "FILM", 4) == 0) {
			fseek(fp, 8, SEEK_CUR);
			char type[4];
			fread(type, 4, 1, fp);
			if (memcmp(type, "FHDR", 4) == 0) {
				fseek(fp, 4, SEEK_CUR);
				char compression[4];
				fread(compression, 4, 1, fp);
				uint32_t height = readInt(fp);
				uint32_t width = readInt(fp);
				out.setup(width, height, &decoder);
			} else if (memcmp(type, "FRME", 4) == 0) {
				uint32_t duration = readInt(fp);
				uint32_t frameSize = readInt(fp);
				uint8_t *frameBuf = (uint8_t *)malloc(frameSize);
				if (frameBuf) {
					fread(frameBuf, 1, frameSize, fp);
					fprintf(stdout, "FRME duration %d frame %d size %d\n", duration, frameSize, size);
					decoder.decode(frameBuf, frameSize);
					free(frameBuf);
					out.dump(path, frmeCounter);
				}
				++frmeCounter;
			} else {
				fprintf(stdout, "Ignoring FILM chunk '%c%c%c%c'\n", type[0], type[1], type[2], type[3]);
			}
		} else {
			fprintf(stdout, "Ignoring tag '%c%c%c%c' size %d\n", tag[0], tag[1], tag[2], tag[3], size);
		}
		fseek(fp, pos + size, SEEK_SET);
	}
}

static int decodeLzss(const uint8_t *src, int len, uint8_t *dst) {
	uint32_t rd = 0, wr = 0;
	int code = 0x100 | src[rd++];
	while (rd < len) {
		const bool bit = (code & 1);
		code >>= 1;
		if (bit) {
			dst[wr++] = src[rd++];
		} else {
			const int code1 = src[rd] | ((src[rd + 1] & 0xF) << 8); // offset
			const int code2 = (src[rd + 1] >> 4) + 3; // len
			rd += 2;
			const uint16_t offset = 0xF000 | code1;
			for (int i = 0; i < code2; ++i) {
				dst[wr] = dst[wr + (int16_t)offset];
				++wr;
			}
		}
		if (code == 1) {
			code = 0x100 | src[rd++];
		}
	}
	return wr;
}

static uint8_t *readFile(FILE *fp, int *size) {
	uint8_t *buf = 0;

	fseek(fp, 0, SEEK_END);
	*size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = (uint8_t *)malloc(*size);
	if (buf) {
		const int count = fread(buf, 1, *size, fp);
		if (count != *size) {
			fprintf(stderr, "Failed to read %d bytes\n", count);
		}
	}
	return buf;
}

static uint8_t _bitmap555[320 * 200 * 2];
static uint16_t _bitmapDei555[320 * 200];

static uint16_t rgb555(const uint8_t *src) {
	const uint16_t color = src[0] * 256 + src[1];
	const int r = (color >> 10) & 31;
	const int g = (color >>  5) & 31;
	const int b = (color >>  0) & 31;
	return (r << 10) | (g << 5) | b;
}

static void deinterlace555(const uint8_t *src, int w, int h, uint16_t *dst) {
	for (int y = 0; y < h / 2; ++y) {
		for (int x = 0; x < w; ++x) {
			dst[x]     = rgb555(&src[0]);
			dst[w + x] = rgb555(&src[2]);
			src += 4;
		}
		dst += w * 2;
	}
}

static void decodeBitmap(FILE *fp, int num) {
	uint8_t *data;
	int dataSize, decodedSize;

	data = readFile(fp, &dataSize);
	if (data) {
		if (memcmp(data, "\x00\xf4\x01\x00", 4) == 0) {
			decodedSize = decodeLzss(data + 4, dataSize - 4, _bitmap555);
			if (decodedSize == sizeof(_bitmap555)) {
				char name[64];
				snprintf(name, sizeof(name), "%s/File%03d.bmp", OUT, num);
				FILE *fp = fopen(name, "wb");
				if (fp) {
					deinterlace555(_bitmap555, 320, 200, _bitmapDei555);
					writeBitmap555(fp, _bitmapDei555, 320, 200);
					fclose(fp);
				}
			} else {
				fprintf(stderr, "Unexpected decoded size %d\n", decodedSize);
			}
		} else {
			fprintf(stderr, "Unexpected header %x%x%x%x\n", data[0], data[1], data[2], data[3]);
		}
		free(data);
	}
}

static int16_t decodeSampleSDX(int16_t prev, int8_t data) {
        const int sqr = (data * ((data < 0) ? -data : data)) * 2;
        return (data & 1) != 0 ? prev + sqr : sqr;
}

static const char *kSDX = "SDX22:1 Squareroot-Delta-Exact compression";

static void decodeSong(FILE *fp, int num) {
	uint8_t buf[64];
	int count, offset, formSize, dataSize;

	count = fread(buf, 1, 12, fp);
	if (count == 12 && memcmp(buf, "FORM", 4) == 0 && memcmp(buf + 8, "AIFC", 4) == 0) {
		formSize = readUint32BE(buf + 4);
		for (offset = 12; offset < formSize; offset += dataSize + 8) {
			fseek(fp, offset, SEEK_SET);

			count = fread(buf, 1, 8, fp);
			dataSize = readUint32BE(buf + 4);

			if (memcmp(buf, "FVER", 4) == 0) {
				continue;
			}
			if (memcmp(buf, "COMM", 4) == 0) {
				assert(dataSize < (int)sizeof(buf));
				count = fread(buf, 1, dataSize, fp);

				const int channels = readUint16BE(buf);
				const int samplesPerFrame = readUint32BE(buf + 2);
				const int bits = readUint16BE(buf + 6);
				int rate = 0;
				{
					const uint8_t *ieee754 = buf + 8;
					const uint32_t m = readUint32BE(ieee754 + 2);
					const int e = 30 - ieee754[1];
					rate = (m >> e);
				}
				if (memcmp(buf + 18, kSDX, strlen(kSDX)) == 0) {
					fprintf(stdout, "bits %d samples %d channels %d rate %d\n", bits, samplesPerFrame, channels, rate);
				}
				continue;
			}
			if (memcmp(buf, "INST", 4) == 0) {
				continue;
			}
			if (memcmp(buf, "MARK", 4) == 0) {
				continue;
			}
			if (memcmp(buf, "SSND", 4) == 0) {
				count = fread(buf, 1, 4, fp);
				// block offset
				count = fread(buf, 1, 4, fp);
				// block size

				int16_t *samples = (int16_t *)calloc(dataSize, sizeof(int16_t));
				if (samples) {
					int16_t sampleL = 0;
					int16_t sampleR = 0;
					for (int i = 0; i < dataSize; i += 2) {
						count = fread(buf, 1, 2, fp);

						samples[i]     = sampleL = decodeSampleSDX(sampleL, buf[0]);
						samples[i + 1] = sampleR = decodeSampleSDX(sampleR, buf[1]);
					}
					char path[MAXPATHLEN];
					snprintf(path, sizeof(path), "%s/song%d.wav", OUT, num);
					FILE *fp = fopen(path, "wb");
					if (fp) {
						writeWav_stereoS16(fp, samples, dataSize);
						fclose(fp);
					}
					free(samples);
				}
				continue;
			}


			fprintf(stdout, "form %c%c%c%c size %d\n", buf[0], buf[1], buf[2], buf[3], dataSize);

		}
	}
}

int main(int argc, char *argv[]) {
	if (argc >= 2) {
		struct stat st;
		if (stat(argv[1], &st) != 0) {
			fprintf(stderr, "Failed to stat '%s'\n", argv[1]);
			return -1;
		}
		if (S_ISREG(st.st_mode)) {
			FILE *fp = fopen(argv[1], "rb");
			if (fp) {
				extractOperaIso(fp);
				fclose(fp);
			} else {
				fprintf(stderr, "Failed to open '%s'\n", argv[1]);
			}
			return 0;
		}
		if (S_ISDIR(st.st_mode)) {
			char path[MAXPATHLEN];
			for (int i = 1; i <= 30; ++i) {
				snprintf(path, sizeof(path), "%s/song%d", argv[1], i);
				FILE *fp = fopen(path, "rb");
				if (fp) {
					decodeSong(fp, i);
					fclose(fp);
				} else {
					fprintf(stderr, "Failed to open '%s'\n", path);
				}
			}
			for (int i = 200; i <= 340; ++i) {
				snprintf(path, sizeof(path), "%s/File%3d", argv[1], i);
				FILE *fp = fopen(path, "rb");
				if (fp) {
					decodeBitmap(fp, i);
					fclose(fp);
				} else {
					fprintf(stderr, "Failed to open '%s'\n", path);
				}
			}
			if (1) {
				static const char *cines[] = { "Logo.Cine", "ootw2.cine", "Spintitle.Cine", 0 };
				for (int i = 0; cines[i]; ++i) {
					snprintf(path, sizeof(path), "%s/%s", argv[1], cines[i]);
					FILE *fp = fopen(path, "rb");
					if (fp) {
						decodeCine(fp, cines[i]);
						fclose(fp);
					} else {
						fprintf(stderr, "Failed to open '%s'\n", path);
					}
				}
			}
			return 0;
		}
	}
	return 0;
}
