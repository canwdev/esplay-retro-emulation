# NES Emulator

## Architecture

```
preview/nes/
├── preview_nes.c           # preview_app_t entry: can_open / open / close
├── preview_nes_platform.c  # OSD layer: audio (I2S), video, input, timer
├── preview_nes_platform.h
├── preview_nes_display.c   # palette + 320×240 RGB565 output (chunked)
├── preview_nes_display.h
├── preview_nes_menu.c      # in-game menu (Continue/Save/Load/Reset/Exit)
├── preview_nes_menu.h
└── nofrendo/               # upstream NES emulator core (Nofrendo)
    ├── nofrendo.c          # main_loop, internal_insert
    ├── cpu/nes6502.c       # 6502 CPU emulation
    ├── nes/nes.c           # nes_emulate (frame loop), nes_create, nes_ppu.c
    ├── nes/nes_rom.c       # ROM loading (rominfo_t)
    ├── sndhrdw/nes_apu.c   # APU audio emulation
    ├── bitmap.c            # bmp_create / frame buffer (malloc'd ~65KB)
    ├── mappers/            # mapper chips (map000 ~ mapvrc)
    ├── config_stub.c       # config stub (config_open returns false)
    └── gui_stub.c          # GUI stub
```

### Data Flow

```
SD Card (.nes file)
    │ appfs_load_file()           ← appfs.c
    ├── write to appfs partition  (14MB on 16MB flash)
    └── esp_partition_mmap()      → CPU-addressable const pointer (zero DRAM)
    │
    ▼ s_rom_data (mmap'd)
rom_load()                       ← nes_rom.c
    ├── rominfo->rom  = s_rom_data     (direct pointer, no copy)
    ├── rominfo->vrom = s_rom_data + … (CHR ROM, direct pointer)
    └── rominfo->vram = malloc(…)      (only if no CHR ROM in file)
    │
    ▼ Nofrendo core
nes_emulate()                    ← nes.c
    ├── nes_renderframe()         → writes to nes.vidbuf (bitmap, 256×240, stride=272)
    └── system_video()            → vid_blit() → vid_custom_blit()
    │
    ▼ vid_custom_blit()           ← preview_nes_platform.c
memcpy(s_lcdfb, bmp->line[0], 256×224)   ← stride 272→256 correction, 61KB heap
nes_display_write(s_lcdfb, NES_SCALE_FILL)
    │
    ▼ nes_display_write()         ← preview_nes_display.c
scale 256×224 → 320×240, apply s_pal (256-entry RGB565 palette)
render in CHUNK_ROWS=24 chunks   ← matches LVGL partial buffer size (7680 px)
10 × hal_display_flush() per frame
    │
    ▼ hal_display_flush()         ← port/esp32/hal_display_esp.c
lv_draw_sw_rgb565_swap()          ← LE→BE byte swap
lcd_draw() → esp_lcd_panel_draw_bitmap() → SPI DMA → ILI9341
```

### Why synchronous (no video queue/task)

The original code used a FreeRTOS queue (depth 1) + a video task on CPU1 to decouple emulation from display. This caused **tearing**: CPU0 writing to `s_lcdfb` while CPU1 read from it in chunks, producing inter-frame artifacts (horizontal stripes).

The current implementation renders synchronously in `vid_custom_blit` on CPU0. Trade-off:
- **No tearing** — single-threaded access to `s_lcdfb`
- **~19 FPS** visible — LCD rendering (~30 ms) inline with emulation (~35 ms)
- **No DMA issues** — 10 × 15KB chunks per frame, same as LVGL

### ROM Loading: AppFS

ROMs are loaded into flash (not DRAM) via a dedicated `appfs` partition (14MB at 0x220000 in `partitions.csv`). The process:

1. `appfs_load_file(path)` → chunked write to flash → `esp_partition_mmap()` → const pointer
2. `rominfo->rom` / `rominfo->vrom` point directly into the mmap'd region (no copy)
3. The emulator reads ROM data through the flash cache transparently
4. On exit: `appfs_release()` → `spi_flash_munmap()`

This eliminates DRAM pressure for ROM storage. Before AppFS, ROMs >80KB could not fit in the ESP32's ~110KB contiguous DRAM blocks.

### Memory Map (NES session, typical)

| Buffer | Size | Location |
|--------|------|----------|
| ROM data | 40KB–512KB | AppFS flash (mmap'd, zero DRAM) |
| `frameBuffer` (bitmap.c) | ~65KB | heap (malloc) |
| `s_lcdfb` | 61KB | heap (platform_malloc) |
| `s_chunk` (display) | 15KB | BSS (static) |
| `s_audio_buf` | 2KB | heap (platform_malloc) |
| `null_page` (CPU) | 4KB | heap (malloc, lazy) |
| NES RAM + others | ~8KB | heap |

## Current Issues

### 1. Color Inaccuracy (RGB/BGR)

**Symptom**: Colors appear shifted/incorrect. NES palette is standard RGB565 (red high bits), but the ILI9341 is configured with `LCD_RGB_ENDIAN_BGR` (blue high bits). The `lv_draw_sw_rgb565_swap()` only handles byte endianness (LE→BE), not component order.

LVGL works correctly because its `lv_color16_t` bitfield layout on little-endian ESP32 places blue in LSBs, and after byte-swap the high bits become red — effectively producing BGR output that matches the LCD's BGR setting.

**Candidates for fix**:
- Investigate exact `lv_color16_t` bit layout on this toolchain to replicate in NES palette
- Or: swap R/B in `nes_display_set_palette` if the toolchain produces standard RGB layout
- Or: use `LCD_RGB_ENDIAN_RGB` (may require LVGL-side changes)

### 2. No Audio

I2S initializes successfully (`NES audio I2S init, rate=32000`), but no sound output. `do_audio_frame()` writes to I2S via `nes_platform_audio_submit()`. Possible causes:
- Volume/gain configuration
- I2S DMA buffer underrun at 32kHz
- APU callback not producing valid data

### 3. Low Visible FPS (~19 FPS)

Synchronous LCD rendering costs ~30ms per frame (10 × 24-line SPI flushes at 40MHz). Combined with NES emulation (~35ms), total is ~50ms → ~19 FPS. This is inherent to the synchronous architecture.

Possible optimizations:
- Render only every Nth frame to LCD (already at every-other-frame)
- Reduce chunk size to use LVGL's partial flush more aggressively
- Use SPI at 80MHz instead of 40MHz (needs hardware validation)

## Key Decisions Record

| Decision | Why |
|----------|-----|
| AppFS for ROM storage | ESP32 DRAM too fragmented for >80KB ROMs |
| Synchronous video (no queue) | Eliminated CPU0/CPU1 tearing on s_lcdfb |
| Chunked 24-row rendering | Matches LVGL partial buffer size (proven stable) |
| s_lcdfb stride correction | Nofrendo bitmap pitch=272, NES frame width=256 |
| malloc for frameBuffer/null_page | Moved from BSS to heap to fix DRAM overflow |
| `vTaskDelay(1)` in NES loop | Prevent TWDT timeout on CPU0 idle task |
| `config_open` returns false | Was returning true → main_loop exited immediately |
