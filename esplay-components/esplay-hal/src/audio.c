#include "audio.h"

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "settings.h"
#include "pin_definitions.h"

static float Volume = 1.0f;
static int volumeLevel = 20;
static int sampleRate;

int audio_volume_get()
{
    return volumeLevel;
}

void audio_volume_set(int value)
{
    if (value > VOLUME_LEVEL_COUNT)
    {
        printf("audio_volume_set: value out of range (%d)\n", value);
        abort();
    }

    volumeLevel = value;
    Volume = (float)(volumeLevel*10) * 0.001f;
    printf("Volume is %0.2f\n",Volume);

    if (volumeLevel == 0)
        audio_amp_disable();
}

void audio_init(int sample_rate)
{
    printf("%s: sample_rate=%d\n", __func__, sample_rate);


    // NOTE: buffer needs to be adjusted per AUDIO_SAMPLE_RATE
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX, // Only TX
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, //2-channels
        .communication_format = I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_STAND_MSB,
        .dma_buf_count = 8,
        //.dma_buf_len = 1472 / 2,  // (368samples * 2ch * 2(short)) = 1472
        .dma_buf_len = 534,                       // (416samples * 2ch * 2(short)) = 1664
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, //Interrupt level 1
        //.use_apll = 1
        };

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
	
     //init DAC pad
     //i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN); //only enable GPIO25
	 
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE //Not used
    };
    i2s_set_pin(I2S_NUM, &pin_config);

    sampleRate = sample_rate;
    int32_t vol = volumeLevel;
    settings_load(SettingAudioVolume, &vol);
    audio_volume_set(vol);
    if(volumeLevel != 0)
        audio_amp_enable();
}

void audio_submit(short *stereoAudioBuffer, int frameCount)
{
    if (volumeLevel != 0)
    {
        if (frameCount <= 0 || stereoAudioBuffer == NULL) {
            return;
        }

        int currentAudioSampleCount = frameCount * 2;

        for (int i = 0; i < currentAudioSampleCount; ++i)
        {
            int sample = stereoAudioBuffer[i] * Volume;
            /*
            if (sample > 32767)
                sample = 32767;
            else if (sample < -32767)
                sample = -32767;
            */
            stereoAudioBuffer[i] = (short)sample;
        }

        size_t len = (size_t)currentAudioSampleCount * sizeof(int16_t);
        size_t count;
        i2s_write(I2S_NUM, (const char *)stereoAudioBuffer, len, &count, portMAX_DELAY);
        if (count != len)
        {
            printf("i2s_write_bytes: count (%u) != len (%u)\n", (unsigned)count, (unsigned)len);
            return;
        }
    }
}

void audio_terminate()
{
    audio_amp_disable();
    i2s_zero_dma_buffer(I2S_NUM);
    i2s_stop(I2S_NUM);
    esp_err_t err = i2s_driver_uninstall(I2S_NUM);
    if (err != ESP_OK) {
        printf("%s: i2s_driver_uninstall: %s\n", __func__, esp_err_to_name(err));
    }
}

void audio_resume()
{
    if (volumeLevel != 0)
        audio_amp_enable();
}

void audio_amp_init(void)
{
    /*
     * AMP_SHDN is GPIO4 on ESPlay, which is also SDMMC DAT1 on ESP32's default slot.
     * Configure it before mounting SD and never reconfigure it while SDMMC is active;
     * changing direction/mux after mount can corrupt the SDMMC bus even in 1-bit mode.
     */
    gpio_set_direction(AMP_SHDN, GPIO_MODE_OUTPUT);
    gpio_set_level(AMP_SHDN, 0);
}

void audio_amp_enable()
{
    gpio_set_level(AMP_SHDN, 1);
}

void audio_amp_disable()
{
    gpio_set_level(AMP_SHDN, 0);
}