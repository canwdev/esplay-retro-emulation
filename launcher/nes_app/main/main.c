#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "gamepad.h"
#include "sdcard.h"
#include "settings.h"

#include "nofrendo.h"
#include "nes/nes.h"
#include "nes/input.h"
#include "nes/state.h"
#include "nes/rom.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ======================================================================== */
/*  Constants                                                               */
/* ======================================================================== */

#define TAG                     "nes_app"
#define AUDIO_SAMPLE_RATE       32000
#define NES_PITCH               272

#define MENU_CONTINUE           0
#define MENU_SAVE               1
#define MENU_LOAD               2
#define MENU_RESET              3
#define MENU_EXIT               4
#define MENU_COUNT              5

#define LCD_HOST                SPI2_HOST
#define LCD_CS                  5
#define LCD_DC                  12
#define LCD_BCKL                27
#define LCD_LEDC_CHAN           LEDC_CHANNEL_0
#define ILI9341_CASET           0x2A
#define ILI9341_PASET           0x2B
#define ILI9341_RAMWR           0x2C

/* ======================================================================== */
/*  Global State                                                            */
/* ======================================================================== */

static spi_device_handle_t spi_dev;
static i2s_chan_handle_t tx_chan;
static uint16_t *fb;
static uint8_t *vidbuf[2];
static int vidbuf_idx;
static uint16_t palette[256];
static int nes_volume;
static char rom_path[512];
static char save_path[512];

/* ======================================================================== */
/*  8x8 bitmap font (ASCII 32-126)                                          */
/* ======================================================================== */

struct font_glyph { char ch; uint8_t data[8]; };

static const struct font_glyph font8x8[95] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}},
    {'"', {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}},
    {'#', {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}},
    {'$', {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}},
    {'%', {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}},
    {'&', {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}},
    {'\'',{0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00}},
    {'(', {0x18,0x30,0x60,0x60,0x60,0x30,0x18,0x00}},
    {')', {0x60,0x30,0x18,0x18,0x18,0x30,0x60,0x00}},
    {'*', {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}},
    {'+', {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}},
    {',', {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}},
    {'-', {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}},
    {'/', {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}},
    {'0', {0x7C,0xCE,0xDE,0xF6,0xE6,0xE6,0x7C,0x00}},
    {'1', {0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00}},
    {'2', {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}},
    {'3', {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}},
    {'4', {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00}},
    {'5', {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}},
    {'6', {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}},
    {'7', {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}},
    {'8', {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}},
    {'9', {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}},
    {':', {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x00}},
    {';', {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x30}},
    {'<', {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}},
    {'=', {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}},
    {'>', {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}},
    {'?', {0x7C,0xC6,0x06,0x1C,0x30,0x00,0x30,0x00}},
    {'@', {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x7C,0x00}},
    {'A', {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00}},
    {'B', {0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00}},
    {'C', {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}},
    {'D', {0xF8,0xCC,0xC6,0xC6,0xC6,0xCC,0xF8,0x00}},
    {'E', {0xFE,0xC0,0xC0,0xF8,0xC0,0xC0,0xFE,0x00}},
    {'F', {0xFE,0xC0,0xC0,0xF8,0xC0,0xC0,0xC0,0x00}},
    {'G', {0x3C,0x66,0xC0,0xDE,0xC6,0x66,0x3E,0x00}},
    {'H', {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}},
    {'I', {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}},
    {'J', {0x3E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}},
    {'K', {0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00}},
    {'L', {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0x00}},
    {'M', {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}},
    {'N', {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}},
    {'O', {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}},
    {'P', {0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0,0x00}},
    {'Q', {0x7C,0xC6,0xC6,0xC6,0xD6,0xCC,0x76,0x00}},
    {'R', {0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0x00}},
    {'S', {0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00}},
    {'T', {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}},
    {'U', {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}},
    {'V', {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}},
    {'W', {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}},
    {'X', {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}},
    {'Y', {0xC6,0xC6,0xC6,0x7C,0x18,0x30,0x60,0x00}},
    {'Z', {0xFE,0x06,0x0C,0x18,0x30,0x60,0xFE,0x00}},
    {'[', {0x7C,0x60,0x60,0x60,0x60,0x60,0x7C,0x00}},
    {'\\',{0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}},
    {']', {0x7C,0x0C,0x0C,0x0C,0x0C,0x0C,0x7C,0x00}},
    {'^', {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}},
    {'`', {0x60,0x30,0x18,0x00,0x00,0x00,0x00,0x00}},
    {'a', {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}},
    {'b', {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xFC,0x00}},
    {'c', {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}},
    {'d', {0x06,0x06,0x7E,0xC6,0xC6,0xC6,0x7E,0x00}},
    {'e', {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}},
    {'f', {0x1C,0x36,0x30,0xFC,0x30,0x30,0x30,0x00}},
    {'g', {0x00,0x00,0x76,0xCE,0xC6,0x7E,0x06,0x7C}},
    {'h', {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x00}},
    {'i', {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}},
    {'j', {0x0C,0x00,0x1C,0x0C,0x0C,0xCC,0xCC,0x78}},
    {'k', {0xC0,0xC0,0xCC,0xD8,0xF0,0xD8,0xCC,0x00}},
    {'l', {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}},
    {'m', {0x00,0x00,0x6C,0xFE,0xD6,0xD6,0xD6,0x00}},
    {'n', {0x00,0x00,0xDC,0xE6,0xC6,0xC6,0xC6,0x00}},
    {'o', {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}},
    {'p', {0x00,0x00,0xFC,0xC6,0xC6,0xFC,0xC0,0xC0}},
    {'q', {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x06}},
    {'r', {0x00,0x00,0xDC,0x66,0x60,0x60,0xF0,0x00}},
    {'s', {0x00,0x00,0x7C,0xC0,0x7C,0x06,0x7C,0x00}},
    {'t', {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00}},
    {'u', {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x7E,0x00}},
    {'v', {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}},
    {'w', {0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00}},
    {'x', {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}},
    {'y', {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0x7C}},
    {'z', {0x00,0x00,0xFE,0x0C,0x18,0x30,0xFE,0x00}},
    {'{', {0x1C,0x30,0x30,0xE0,0x30,0x30,0x1C,0x00}},
    {'|', {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}},
    {'}', {0xE0,0x30,0x30,0x1C,0x30,0x30,0xE0,0x00}},
    {'~', {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}},
};

static const char *menu_labels[MENU_COUNT] = {
    "Continue",
    "Save State",
    "Load State",
    "Reset",
    "Exit",
};

/* ======================================================================== */
/*  Hardware Init (Display + Audio)                                         */
/* ======================================================================== */

static void spi_write_cmd(uint8_t cmd)
{
    spi_transaction_t t = {.flags = SPI_TRANS_USE_TXDATA, .length = 8,
                           .tx_data = {cmd}, .user = (void *)0};
    gpio_set_level(LCD_DC, 0);
    spi_device_polling_transmit(spi_dev, &t);
}

static void spi_write_data8(uint8_t data)
{
    spi_transaction_t t = {.flags = SPI_TRANS_USE_TXDATA, .length = 8,
                           .tx_data = {data}, .user = (void *)0};
    gpio_set_level(LCD_DC, 1);
    spi_device_polling_transmit(spi_dev, &t);
}

static void spi_write_data16(uint16_t data)
{
    uint8_t buf[2] = {data >> 8, data & 0xFF};
    spi_transaction_t t = {.length = 16, .tx_buffer = buf, .user = (void *)0};
    gpio_set_level(LCD_DC, 1);
    spi_device_polling_transmit(spi_dev, &t);
}

static void ili9341_set_window(int x0, int y0, int x1, int y1)
{
    spi_write_cmd(ILI9341_CASET);
    spi_write_data16(x0);
    spi_write_data16(x1);
    spi_write_cmd(ILI9341_PASET);
    spi_write_data16(y0);
    spi_write_data16(y1);
    spi_write_cmd(ILI9341_RAMWR);
}

static void display_init(void)
{
    gpio_set_direction(LCD_DC, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .mosi_io_num = 23, .miso_io_num = -1, .sclk_io_num = 18,
        .max_transfer_sz = 4096, .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0, .spics_io_num = LCD_CS,
        .queue_size = 2, .flags = SPI_DEVICE_NO_DUMMY,
    };
    spi_bus_add_device(LCD_HOST, &devcfg, &spi_dev);

    /* ILI9341 init sequence (from retro-go esplay-micro config.h) */
    static const uint8_t init_seq[] = {
        0x01, 0x80, 0x96, /* SWRESET, delay 150ms */
        0xCF, 3, 0x00, 0xC1, 0x30,
        0xED, 4, 0x64, 0x03, 0x12, 0x81,
        0xE8, 3, 0x85, 0x00, 0x78,
        0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
        0xF7, 1, 0x20,
        0xEA, 2, 0x00, 0x00,
        0xC0, 1, 0x23, /* Power Control 1 */
        0xC1, 1, 0x10, /* Power Control 2 */
        0xC5, 2, 0x3E, 0x28, /* VCOM Control 1 */
        0xC7, 1, 0x86, /* VCOM Control 2 */
        0x36, 1, 0x28, /* MADCTL: MY=1, BGR=1 */
        0x37, 1, 0x00, /* VSCRSADD */
        0x3A, 1, 0x55, /* COLMOD: 16bpp */
        0xB1, 2, 0x00, 0x18, /* Frame rate control, 61Hz */
        0xB6, 3, 0x08, 0x82, 0x27, /* Display Function */
        0xF2, 1, 0x00, /* Enable 3G */
        0x26, 1, 0x01, /* Gamma */
        0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
        0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
        0x11, 0x80, 0x78, /* SLPOUT, delay 120ms */
        0x29, 0x00, 0x00, /* DISPON */
    };

    size_t i = 0;
    while (i < sizeof(init_seq)) {
        uint8_t cmd = init_seq[i++];
        uint8_t n = init_seq[i++];
        uint8_t delay = 0;

        if (n & 0x80) {
            delay = init_seq[i++];
            n = 0;
        }

        spi_write_cmd(cmd);
        for (int j = 0; j < n; j++)
            spi_write_data8(init_seq[i++]);
        if (delay > 0)
            vTaskDelay(pdMS_TO_TICKS(delay));
    }

    ili9341_set_window(0, 0, 319, 239);

    /* Backlight PWM */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);
    ledc_channel_config_t ledc_ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LCD_LEDC_CHAN,
        .timer_sel = LEDC_TIMER_0, .gpio_num = LCD_BCKL,
        .duty = 1023, .hpoint = 0};
    ledc_channel_config(&ledc_ch);

    ESP_LOGI(TAG, "Display init OK");
}

static void display_set_brightness(uint8_t pct)
{
    if (pct > 100) pct = 100;
    uint32_t duty = (pct * 1023) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN);
}

static void display_flush(int x0, int y0, int x1, int y1, void *px)
{
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    const int CHUNK_H = 8;
    for (int y = y0; y < y1; y += CHUNK_H) {
        int ch = (y + CHUNK_H > y1) ? y1 - y : CHUNK_H;

        ili9341_set_window(x0, y, x1 - 1, y + ch - 1);

        int bytes = w * ch * 2;
        const uint8_t *src = (const uint8_t *)px + (y - y0) * w * 2;
        int sent = 0;
        while (sent < bytes) {
            int chunk = bytes - sent;
            if (chunk > 2048) chunk = 2048;
            spi_transaction_t t = {.length = chunk * 8, .tx_buffer = src + sent,
                                   .user = (void *)0};
            gpio_set_level(LCD_DC, 1);
            spi_device_polling_transmit(spi_dev, &t);
            sent += chunk;
        }
    }
}

/* ======================================================================== */
/*  Audio                                                                   */
/* ======================================================================== */

static void audio_init_32k(void)
{
    gpio_config_t amp_cfg = {
        .pin_bit_mask = BIT64(CONFIG_AUDIO_AMP_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&amp_cfg);
    gpio_set_level(CONFIG_AUDIO_AMP_GPIO, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_APLL,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_AUDIO_I2S_BCLK_GPIO,
            .ws = CONFIG_AUDIO_I2S_WS_GPIO,
            .dout = CONFIG_AUDIO_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    gpio_set_level(CONFIG_AUDIO_AMP_GPIO, 1);
    ESP_LOGI(TAG, "I2S audio init OK, sample_rate=%d", AUDIO_SAMPLE_RATE);
}

static void audio_write(const int16_t *mono, size_t frames)
{
    if (!tx_chan || mono == NULL || frames == 0)
        return;

    int gain = nes_volume;
    if (gain <= 0) gain = 0;
    if (gain > 100) gain = 100;

    if (gain == 0) {
        size_t total = frames * 2 * sizeof(int16_t);
        size_t skip_written = 0;
        static int16_t silence[512 * 2];
        while (skip_written < total) {
            size_t n = 0;
            i2s_channel_write(tx_chan, (uint8_t *)silence,
                              total - skip_written, &n, pdMS_TO_TICKS(50));
            if (n == 0) break;
            skip_written += n;
        }
        return;
    }

    static int16_t stereo[512 * 2];
    size_t max_frames = (sizeof(stereo) / sizeof(stereo[0])) / 2;
    if (frames > max_frames) frames = max_frames;
    for (size_t i = 0; i < frames; i++) {
        int16_t s = (int16_t)((int32_t)mono[i] * gain / 100);
        stereo[i * 2] = s;
        stereo[i * 2 + 1] = s;
    }

    size_t total = frames * 2 * sizeof(int16_t);
    size_t written = 0;
    while (written < total) {
        size_t n = 0;
        esp_err_t err = i2s_channel_write(tx_chan, (uint8_t *)stereo + written,
                                          total - written, &n, pdMS_TO_TICKS(100));
        if (err != ESP_OK) break;
        if (n == 0) break;
        written += n;
    }
}

static void audio_deinit_32k(void)
{
    if (tx_chan) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }
    gpio_set_level(CONFIG_AUDIO_AMP_GPIO, 0);
}

/* ======================================================================== */
/*  Display Rendering                                                       */
/* ======================================================================== */

static void display_render(uint8_t *nes_pixels, int pitch)
{
    int x_scale = (256 << 16) / 320;
    int crop = 8;

    for (int y = 0; y < 240; y++) {
        uint8_t *src = nes_pixels + y * pitch + crop;
        for (int x = 0; x < 320; x++) {
            int sx = (x * x_scale) >> 16;
            fb[y * 320 + x] = palette[src[sx]];
        }
    }

    display_flush(0, 0, 320, 240, fb);
}

static void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            fb[(y + dy) * 320 + (x + dx)] = color;
        }
    }
}

static void display_draw_text(int x, int y, const char *str, uint16_t color, bool inverted)
{
    uint16_t bg = inverted ? color : 0x0000;
    uint16_t fg = inverted ? 0x0000 : color;

    while (*str) {
        int ci = *str - ' ';
        if (ci < 0 || ci >= 95) { str++; x += 8; continue; }

        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8[ci].data[row];
            for (int col = 0; col < 8; col++) {
                uint16_t c = (bits & (0x80 >> col)) ? fg : bg;
                fb[(y + row) * 320 + (x + col)] = c;
            }
        }

        str++;
        x += 8;
    }
}

static void display_darken(void)
{
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = fb[i];
        uint16_t r = ((c >> 11) & 0x1F) >> 1;
        uint16_t g = ((c >> 5) & 0x3F) >> 1;
        uint16_t b = (c & 0x1F) >> 1;
        fb[i] = (r << 11) | (g << 5) | b;
    }
}

/* ======================================================================== */
/*  Input                                                                   */
/* ======================================================================== */

static int gamepad_to_nes(const input_gamepad_state *gp)
{
    int buttons = 0;
    if (gp->values[GAMEPAD_INPUT_UP])     buttons |= NES_PAD_UP;
    if (gp->values[GAMEPAD_INPUT_DOWN])   buttons |= NES_PAD_DOWN;
    if (gp->values[GAMEPAD_INPUT_LEFT])   buttons |= NES_PAD_LEFT;
    if (gp->values[GAMEPAD_INPUT_RIGHT])  buttons |= NES_PAD_RIGHT;
    if (gp->values[GAMEPAD_INPUT_START])  buttons |= NES_PAD_START;
    if (gp->values[GAMEPAD_INPUT_SELECT]) buttons |= NES_PAD_SELECT;
    if (gp->values[GAMEPAD_INPUT_A])      buttons |= NES_PAD_A;
    if (gp->values[GAMEPAD_INPUT_B])      buttons |= NES_PAD_B;
    return buttons;
}

/* ======================================================================== */
/*  Menu                                                                    */
/* ======================================================================== */

static int nes_menu(void)
{
    int sel = 0;
    input_gamepad_state gp;
    input_gamepad_state prev = {0};

    static uint16_t fb_backup[320 * 240];
    memcpy(fb_backup, fb, sizeof(fb_backup));

    display_darken();

    while (1) {
        gamepad_read(&gp);

        if (gp.values[GAMEPAD_INPUT_UP] && !prev.values[GAMEPAD_INPUT_UP]) {
            sel = (sel > 0) ? sel - 1 : MENU_COUNT - 1;
        }
        if (gp.values[GAMEPAD_INPUT_DOWN] && !prev.values[GAMEPAD_INPUT_DOWN]) {
            sel = (sel < MENU_COUNT - 1) ? sel + 1 : 0;
        }
        if (gp.values[GAMEPAD_INPUT_A] && !prev.values[GAMEPAD_INPUT_A]) {
            break;
        }
        if (gp.values[GAMEPAD_INPUT_MENU] && !prev.values[GAMEPAD_INPUT_MENU]) {
            sel = MENU_CONTINUE;
            break;
        }

        memcpy(fb, fb_backup, sizeof(fb_backup));
        display_darken();

        int menu_w = 160, menu_h = MENU_COUNT * 20 + 24;
        int mx = (320 - menu_w) / 2, my = (240 - menu_h) / 2;

        display_fill_rect(mx, my, menu_w, menu_h, 0x0821);
        display_draw_text(mx + 8, my + 6, "NES MENU", 0xFFFF, false);

        for (int i = 0; i < MENU_COUNT; i++) {
            int iy = my + 22 + i * 18;
            bool hilite = (i == sel);
            display_draw_text(mx + 12, iy + 2, menu_labels[i],
                              hilite ? 0xFFFF : 0xAD55, hilite);
        }

        display_flush(0, 0, 320, 240, fb);

        prev = gp;
        vTaskDelay(pdMS_TO_TICKS(16));
    }

    memcpy(fb, fb_backup, sizeof(fb_backup));
    display_flush(0, 0, 320, 240, fb);

    return sel;
}

/* ======================================================================== */
/*  Save paths                                                              */
/* ======================================================================== */

static char sram_path[512];

static void make_save_path(const char *rom_path)
{
    const char *name = strrchr(rom_path, '/');
    name = name ? name + 1 : rom_path;

    strlcpy(save_path, "/sd/esplay/saves/", sizeof(save_path));
    size_t base = strlen(save_path);

    const char *dot = strrchr(name, '.');
    if (dot) {
        memcpy(save_path + base, name, (size_t)(dot - name));
        save_path[base + (dot - name)] = '\0';
    } else {
        strlcat(save_path, name, sizeof(save_path));
    }

    strlcpy(sram_path, save_path, sizeof(sram_path));
    strlcat(save_path, ".sav", sizeof(save_path));
    strlcat(sram_path, ".sram", sizeof(sram_path));

    mkdir("/sd/esplay", 0755);
    mkdir("/sd/esplay/saves", 0755);
}

/* ======================================================================== */
/*  Game Loop                                                               */
/* ======================================================================== */

static void run_game_loop(nes_t *nes)
{
    input_gamepad_state gp;
    int skip = 0;
    int frame_count = 0;
    int64_t last_sram_save = 0;
    bool menu_was_pressed = false;

    while (1) {
        gamepad_read(&gp);

        if (gp.values[GAMEPAD_INPUT_MENU]) {
            if (!menu_was_pressed) {
                menu_was_pressed = true;
                int action = nes_menu();
                switch (action) {
                case MENU_SAVE:
                    state_save(save_path);
                    ESP_LOGI(TAG, "State saved: %s", save_path);
                    break;
                case MENU_LOAD:
                    state_load(save_path);
                    ESP_LOGI(TAG, "State loaded: %s", save_path);
                    break;
                case MENU_RESET:
                    nes_reset(true);
                    ESP_LOGI(TAG, "NES reset");
                    break;
                case MENU_EXIT:
                    ESP_LOGI(TAG, "Exiting NES");
                    return;
                case MENU_CONTINUE:
                default:
                    break;
                }
                skip = 0;
            }
        } else {
            menu_was_pressed = false;
        }

        int buttons = gamepad_to_nes(&gp);
        input_update(0, buttons);

        bool draw = (skip == 0);
        int64_t t0 = esp_timer_get_time();

        if (draw) {
            vidbuf_idx ^= 1;
            nes_setvidbuf(vidbuf[vidbuf_idx]);
        }
        nes_emulate(draw);
        if (draw) {
            display_render(vidbuf[vidbuf_idx], NES_PITCH);
        }

        audio_write(nes->apu->buffer, nes->apu->samples_per_frame);

        int64_t elapsed = esp_timer_get_time() - t0;
        if (skip == 0 && elapsed > 18333 + 1500) {
            skip = 1;
        } else if (skip > 0) {
            skip--;
        }

        if (nes->cart && nes->cart->battery) {
            int64_t now = esp_timer_get_time();
            if (now - last_sram_save > 5000000) {
                rom_savesram(sram_path);
                last_sram_save = now;
            }
        }

        if (++frame_count > 1) {
            vTaskDelay(1);
            frame_count = 0;
        }
    }
}

/* ======================================================================== */
/*  Entry Point                                                             */
/* ======================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "NES app starting");

    int32_t brightness = 50;
    int32_t volume = 50;
    settings_init();
    settings_load(SettingBacklight, &brightness);
    settings_load(SettingAudioVolume, &volume);

    rom_path[0] = '\0';
    nvs_handle_t boot_nvs;
    if (nvs_open("boot", NVS_READONLY, &boot_nvs) == ESP_OK) {
        size_t len = sizeof(rom_path);
        nvs_get_str(boot_nvs, "boot_path", rom_path, &len);
        nvs_close(boot_nvs);
    }
    ESP_LOGI(TAG, "Boot ROM path: %s", rom_path);

    const esp_partition_t *lp = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "launcher");
    if (lp) {
        esp_ota_set_boot_partition(lp);
        ESP_LOGI(TAG, "OTA set back to launcher");
    }

    ESP_LOGI(TAG, "Mounting SD card...");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_err_t sd_err = sdcard_open("/sd");
    ESP_LOGI(TAG, "SD card mount result: %d", (int)sd_err);

    if (rom_path[0] == '\0') {
        ESP_LOGW(TAG, "No ROM path, returning to launcher");
        goto exit;
    }

    nes_t *nes = nes_init(SYS_DETECT, AUDIO_SAMPLE_RATE, true, NULL);
    if (!nes) {
        ESP_LOGE(TAG, "nes_init failed");
        goto exit;
    }

    int load_ret = nes_loadfile(rom_path);
    if (load_ret < 0) {
        ESP_LOGE(TAG, "nes_loadfile failed: %d", load_ret);
        nes_shutdown();
        goto exit;
    }
    ESP_LOGI(TAG, "ROM loaded: %s", rom_path);

    vidbuf[0] = malloc(NES_PITCH * 240);
    vidbuf[1] = malloc(NES_PITCH * 240);
    if (!vidbuf[0] || !vidbuf[1]) {
        ESP_LOGE(TAG, "Vidbuf alloc failed");
        goto cleanup;
    }

    fb = malloc(320 * 240 * sizeof(uint16_t));
    if (!fb) {
        ESP_LOGE(TAG, "Framebuffer alloc failed");
        goto cleanup;
    }

    {
        uint16_t *pal = nofrendo_buildpalette(NES_PALETTE_PVM, 16);
        if (pal) {
            memcpy(palette, pal, 256 * sizeof(uint16_t));
            free(pal);
        }
    }

    make_save_path(rom_path);
    if (nes->cart && nes->cart->battery) {
        rom_loadsram(sram_path);
    }

    display_init();
    display_set_brightness((uint8_t)brightness);
    ESP_LOGI(TAG, "Display init OK, brightness=%d", (int)brightness);

    gamepad_init();
    ESP_LOGI(TAG, "Gamepad init OK");

    audio_init_32k();
    nes_volume = (int)volume;
    ESP_LOGI(TAG, "Audio init OK, volume=%d", nes_volume);

    ESP_LOGI(TAG, "Entering game loop");
    run_game_loop(nes);

    if (nes->cart && nes->cart->battery) {
        rom_savesram(sram_path);
    }

cleanup:
    free(fb);
    free(vidbuf[0]);
    free(vidbuf[1]);
    nes_shutdown();

exit:
    sdcard_close();
    audio_deinit_32k();
    ESP_LOGI(TAG, "Restarting to launcher");
    esp_restart();
}
