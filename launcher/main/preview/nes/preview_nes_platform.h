#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void nes_platform_init(const char *rom_path);
void nes_platform_deinit(void);
void nes_platform_game_loop(void);
const char *nes_platform_rom_path(void);
