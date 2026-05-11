#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
	AudioCodecUnknown = 0,
	AudioCodecMP3,
} AudioCodec;

typedef struct {
	uint32_t sample_rate;
	uint32_t channels;
	size_t buf_size;
} AudioInfo;

typedef struct AudioDecoder {
	int (*open)(void **ctx, const char *path);
	int (*close)(void *ctx);
	int (*get_info)(void *ctx, AudioInfo *info);
	int (*decode)(void *ctx, int16_t *pcm, int channels, size_t frame_count);
} AudioDecoder;

AudioDecoder *acodec_get_decoder(AudioCodec codec);
