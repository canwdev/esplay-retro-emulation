#include "acodecs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_heap_caps.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

/* ~one MPEG1 Layer III granule; smaller reads reduce long SDMMC bursts under audio load. */
#define MP3_DECODE_FRAMES 1152
#define MP3_IO_BUFFER_SIZE (16 * 1024)

typedef struct {
	drmp3 mp3;
	FILE *fp;
	uint8_t *io_buf;
	size_t io_buf_len;
	size_t io_buf_pos;
	long io_buf_file_pos;
	long logical_pos;
	long file_size;
	int16_t *mono_scratch;
	size_t mono_scratch_frames;
} Mp3Ctx;

static size_t mp3_refill_io_buf(Mp3Ctx *c)
{
	if (!c || !c->fp || !c->io_buf) {
		return 0;
	}

	if (fseek(c->fp, c->logical_pos, SEEK_SET) != 0) {
		return 0;
	}

	c->io_buf_file_pos = c->logical_pos;
	c->io_buf_pos = 0;
	c->io_buf_len = 0;

	for (int attempt = 0; attempt < 3; attempt++) {
		clearerr(c->fp);
		size_t n = fread(c->io_buf, 1, MP3_IO_BUFFER_SIZE, c->fp);
		if (n > 0 || feof(c->fp)) {
			c->io_buf_len = n;
			return n;
		}
		usleep(2 * 1000);
	}

	return 0;
}

static size_t mp3_read_cb(void *user_data, void *buffer_out, size_t bytes_to_read)
{
	Mp3Ctx *c = (Mp3Ctx *)user_data;
	if (!c || !c->fp || !buffer_out || bytes_to_read == 0) {
		return 0;
	}

	size_t total = 0;
	while (total < bytes_to_read) {
		long cache_end = c->io_buf_file_pos + (long)c->io_buf_len;
		bool cache_hit = c->io_buf_len > 0 &&
				c->logical_pos >= c->io_buf_file_pos &&
				c->logical_pos < cache_end;

		if (!cache_hit) {
			if (mp3_refill_io_buf(c) == 0) {
				break;
			}
		}

		c->io_buf_pos = (size_t)(c->logical_pos - c->io_buf_file_pos);
		size_t available = c->io_buf_len - c->io_buf_pos;
		size_t wanted = bytes_to_read - total;
		size_t n = available < wanted ? available : wanted;
		if (n == 0) {
			continue;
		}

		memcpy((uint8_t *)buffer_out + total, c->io_buf + c->io_buf_pos, n);
		c->io_buf_pos += n;
		c->logical_pos += (long)n;
		total += n;
	}
	return total;
}

static drmp3_bool32 mp3_seek_cb(void *user_data, int offset, drmp3_seek_origin origin)
{
	Mp3Ctx *c = (Mp3Ctx *)user_data;
	if (!c || !c->fp) {
		return DRMP3_FALSE;
	}

	int whence = SEEK_SET;
	long base = 0;
	if (origin == DRMP3_SEEK_CUR) {
		whence = SEEK_CUR;
		base = c->logical_pos;
	} else if (origin == DRMP3_SEEK_END) {
		whence = SEEK_END;
		base = c->file_size;
	} else {
		whence = SEEK_SET;
	}
	(void)whence;

	long new_pos = base + offset;
	if (new_pos < 0 || (c->file_size >= 0 && new_pos > c->file_size)) {
		return DRMP3_FALSE;
	}

	c->logical_pos = new_pos;
	if (!(c->io_buf_len > 0 &&
			c->logical_pos >= c->io_buf_file_pos &&
			c->logical_pos <= c->io_buf_file_pos + (long)c->io_buf_len)) {
		c->io_buf_len = 0;
		c->io_buf_pos = 0;
	}
	return DRMP3_TRUE;
}

static drmp3_bool32 mp3_tell_cb(void *user_data, drmp3_int64 *cursor)
{
	Mp3Ctx *c = (Mp3Ctx *)user_data;
	if (!c || !c->fp || !cursor) {
		return DRMP3_FALSE;
	}
	long pos = c->logical_pos;
	if (pos < 0) {
		return DRMP3_FALSE;
	}
	*cursor = (drmp3_int64)pos;
	return DRMP3_TRUE;
}

static int mp3_open(void **ctx, const char *path)
{
	Mp3Ctx *c = calloc(1, sizeof(Mp3Ctx));
	if (!c) {
		return -1;
	}
	c->fp = fopen(path, "rb");
	if (!c->fp) {
		free(c);
		return -1;
	}
	c->file_size = -1;
	if (fseek(c->fp, 0, SEEK_END) == 0) {
		c->file_size = ftell(c->fp);
	}
	(void)fseek(c->fp, 0, SEEK_SET);
	c->logical_pos = 0;
	c->io_buf = heap_caps_malloc(MP3_IO_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (!c->io_buf) {
		fclose(c->fp);
		free(c);
		return -1;
	}
	if (!drmp3_init(&c->mp3, mp3_read_cb, mp3_seek_cb, mp3_tell_cb, NULL, c, NULL)) {
		free(c->io_buf);
		fclose(c->fp);
		free(c);
		return -1;
	}
	c->mono_scratch_frames = MP3_DECODE_FRAMES;
	c->mono_scratch = malloc(c->mono_scratch_frames * sizeof(int16_t));
	if (c->mp3.channels == 1 && !c->mono_scratch) {
		drmp3_uninit(&c->mp3);
		free(c->io_buf);
		fclose(c->fp);
		free(c);
		return -1;
	}
	*ctx = c;
	return 0;
}

static int mp3_close(void *ctx)
{
	Mp3Ctx *c = (Mp3Ctx *)ctx;
	if (!c) {
		return -1;
	}
	drmp3_uninit(&c->mp3);
	if (c->fp) {
		fclose(c->fp);
	}
	free(c->io_buf);
	free(c->mono_scratch);
	free(c);
	return 0;
}

static int mp3_get_info(void *ctx, AudioInfo *info)
{
	Mp3Ctx *c = (Mp3Ctx *)ctx;
	if (!c || !info) {
		return -1;
	}
	info->sample_rate = c->mp3.sampleRate;
	info->channels = 2;
	info->buf_size = MP3_DECODE_FRAMES;
	return 0;
}

static int mp3_decode(void *ctx, int16_t *pcm, int channels, size_t frame_count)
{
	Mp3Ctx *c = (Mp3Ctx *)ctx;
	if (!c || !pcm || channels != 2) {
		return -1;
	}
	if (frame_count > c->mono_scratch_frames && c->mp3.channels == 1) {
		return -1;
	}

	if (c->mp3.channels == 2) {
		drmp3_uint64 n = drmp3_read_pcm_frames_s16(&c->mp3, (drmp3_uint64)frame_count, pcm);
		return (int)n;
	}

	drmp3_uint64 n = drmp3_read_pcm_frames_s16(&c->mp3, (drmp3_uint64)frame_count, c->mono_scratch);
	for (drmp3_uint64 i = 0; i < n; i++) {
		int16_t s = c->mono_scratch[i];
		pcm[(size_t)i * 2] = s;
		pcm[(size_t)i * 2 + 1] = s;
	}
	return (int)n;
}

static AudioDecoder mp3_decoder = {
		.open = mp3_open,
		.close = mp3_close,
		.get_info = mp3_get_info,
		.decode = mp3_decode,
};

AudioDecoder *acodec_get_decoder(AudioCodec codec)
{
	if (codec == AudioCodecMP3) {
		return &mp3_decoder;
	}
	return NULL;
}
