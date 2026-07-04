# NES Emulator (retro-go nofrendo)

移植自 [retro-go](https://github.com/ducalex/retro-go) 项目的 nofrendo NES 模拟核心。

## Architecture

```
preview/nes/
├── preview_nes.c           # preview_app_t entry: can_open / open / close
├── preview_nes_platform.c  # OSD layer: audio (I2S), video blit, input, save, game loop
├── preview_nes_platform.h
├── preview_nes_display.c   # Async display task: 256→224→320×240 scaling + chunk flush
├── preview_nes_display.h
├── preview_nes_menu.c      # Bitmap font in-game menu (Continue/Save/Load/Reset/Exit)
├── preview_nes_menu.h
├── README.md
└── nes_core/               # retro-go nofrendo core (58 mappers, FDS/NSF/database)
    ├── nofrendo.c/h        # nofrendo_init / nofrendo_buildpalette (6 palettes)
    ├── palettes.h          # 6 NES palettes + GUI palette
    ├── database.h          # Mesen game database (~5000 entries)
    ├── config.h
    ├── nes/
    │   ├── nes.c/h         # nes_emulate(bool draw) — single-frame execution
    │   ├── ppu.c/h         # PPU: ppu_renderline
    │   ├── apu.c/h         # APU: apu_emulate, apu_fc_advance
    │   ├── cpu.c/h         # 6502: nes6502_execute
    │   ├── mem.c/h         # Memory subsystem
    │   ├── rom.c/h         # rom_loadmem (zero-copy from AppFS mmap)
    │   ├── mmc.c/h         # Mapper controller
    │   ├── input.c/h       # input_update(port, NES_PAD_* bits)
    │   ├── state.c/h       # state_save/state_load (SNSS format)
    │   └── utils.h         # Platform abstraction (logging, IRAM_ATTR)
    └── mappers/            # 58 mapper implementations
```

## Data Flow

```
SD Card (.nes file)
    │ appfs_load_file()           ← appfs.c
    ├── write to appfs partition  (14MB on 16MB flash)
    └── esp_partition_mmap()      → zero-DRAM pointer
    │
    ▼ rom_data (mmap'd, no copy)
rom_loadmem(rom_data, size)       ← nes_core/nes/rom.c
    ├── rom.data_ptr = rom_data   (direct pointer, free_data_ptr=false)
    ├── rom.prg_rom = data+offset (direct pointer)
    └── rom.chr_rom = data+offset (direct pointer)
    │
    ▼ rom_t *cart
nes_insertcart(cart)              ← nes_core/nes/nes.c
    ├── mmc_init(cart)            (select mapper chip, 58 supported)
    ├── Detects NTSC/PAL timing
    └── nes_reset(true)
    │
    ▼ nes_t *nes (static singleton)
Game Loop (CPU 0):
    for (;;) {
        input_update(0, buttons)      ← NES_PAD_* bitmask
        if (drawFrame)
            nes_setvidbuf(s_vidbuf)   ← internal SRAM buffer
        nes_emulate(drawFrame)        ← single frame (PPU → vidbuf → blit → display)
        i2s_channel_write(apu_buffer) ← audio output
        if (elapsed > target+1500us)
            skip = 1                  ← auto frame-skip
        vTaskDelay(1) every 4s        ← WDT safety
    }

Async Display Task (CPU 1, prio 3, 8192B stack):
    while (s_disp_running) {
        wait frame semaphore
        scale 256×224 → 320×240 (fixed-point ratio)
        chunk-level hash partial update (24-row chunks)
        hal_display_flush() → lv_draw_sw_rgb565_swap() → SPI DMA → ILI9341
        signal done semaphore
    }
```

## Rendering Pipeline

```
PPU renders 256×224 to s_vidbuf (272-stride with 8px overdraw)
    │ blit_screen(vidbuf)                 ← called from nes_emulate()
    ▼
nes_display_submit(vidbuf, pitch=272, scale)
    ├── non-blocking: if display busy → drop (game loop auto frame-skip compensates)
    ├── memcpy 60KB to display buffer s_fb
    └── signal display task on CPU 1
        │
        ▼ display_task (CPU 1, async)
nes_display_render(data, stride, scale)
    ├── Scale 256×224 → 320×240 (fixed-point ratio)
    ├── Palette lookup: s_pal[src[x2]] → RGB565
    ├── Render in CHUNK_ROWS=24 line bands → s_chunk (15KB)
    ├── Chunk hash comparison (DJB2, 32-bit)
    └── Changed chunks: hal_display_flush() → lv_draw_sw_rgb565_swap() → lcd_draw() → SPI DMA
```

## Performance

| Metric | Before | After |
|--------|--------|-------|
| Emulation time | ~50ms | ~16ms(skip)/~42ms(render) |
| Visible FPS | ~13 | ~30-37 |
| ROM storage | AppFS flash mmap (zero DRAM) | Same |
| PPU buffer | PSRAM (malloc) | Internal SRAM (heap_caps, fast!) |
| Display | Sync blocking | Async CPU 1 with chunk partial updates |

## Key Design Decisions

- **Audio NOT used as clock**: emulation is slower than real-time (42ms vs 16.6ms target), so audio pacing doesn't work. Instead use `elapsed > target + 1500us` adaptive frame-skip.
- **vTaskDelay(1) every 4s**: minimal yield to let IDLE0 reset the task watchdog. Not needed every frame because game loop is CPU-bound.
- **s_vidbuf = internal SRAM**: `heap_caps_malloc(MALLOC_CAP_INTERNAL)` for fast PPU writes, falls back to `platform_malloc` (PSRAM) if internal SRAM exhausted.
- **s_fb = PSRAM**: display copy buffer uses `platform_malloc` (PSRAM), low priority since only memcpy'd.

## Known Issues

- **横纹 (screen tearing)**: ILI9341 has no TE (Tearing Effect) signal. Without hardware vsync, tearing is inherent at any non-60Hz frame rate. Mitigated by stable 30+ FPS and async display task on separate core.
- **Color inaccuracy**: `lv_draw_sw_rgb565_swap()` handles LE→BE byte swap but ILI9341 uses `LCD_RGB_ENDIAN_BGR` MADCTL. The R/B swap matches LVGL's internal color format. Slight color shifts persist (known project issue).
- **No CRC32**: `database.h` disabled (CRC32 always 0), ROMs identified by iNES headers only. Implementing CRC32 (~30 lines) would enable the game database for accurate mapper/system detection.
- **~42ms emulation**: the 6502/PPU emulation is the fundamental bottleneck. Further optimization requires profiling the hot paths (nes6502_execute, ppu_renderline) or native assembly.

## Compilation

Compiled with `-O3` for the NES core. Binary size ~1.27MB (40% of 2MB app partition free).
