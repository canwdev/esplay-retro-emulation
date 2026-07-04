#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---- ROM data ---- */
uint8_t *nes_platform_get_rom_data(void);
void nes_platform_set_rom_data(uint8_t *data);
const char *nes_platform_rom_path(void);

/* ---- Lifecycle ---- */
void nes_platform_init(const char *rom_path);
void nes_platform_deinit(void);

/* ---- Audio ---- */
void nes_platform_audio_init(void);
void nes_platform_audio_deinit(void);
void nes_platform_audio_submit(const int16_t *samples, size_t count);
int  nes_platform_audio_get_volume(void);
void nes_platform_audio_set_volume(int vol);

/* ---- Video ---- */
void nes_platform_video_submit(const uint8_t *frame_buf, int mode);
void nes_platform_video_signal_menu(void);
void nes_platform_video_task_stop(void);

/* ---- Timer ---- */
void nes_platform_timer_init(void);
void nes_platform_timer_deinit(void);

/* ---- Palette ---- */
const uint16_t *nes_platform_get_palette(void);

/* ---- Save/Load ---- */
void nes_platform_save_path(const char *rom_path, char *out, size_t out_len);
void nes_platform_ensure_save_dir(void);
