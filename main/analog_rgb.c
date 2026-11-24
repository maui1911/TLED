/*
 * TLED - Matter-over-Thread LED Controller
 * Analog RGB (PWM) output driver implementation
 */

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <esp_log.h>
#include <esp_check.h>
#include <driver/ledc.h>

#include "analog_rgb.h"

static const char *TAG = "analog_rgb";

// 10-bit duty at 4 kHz: no visible flicker, outside the audible range for most
// lamps, and well within what a small logic-level MOSFET switches cleanly.
#define ANALOG_RGB_FREQ_HZ      4000
#define ANALOG_RGB_RESOLUTION   LEDC_TIMER_10_BIT
#define ANALOG_RGB_MAX_DUTY     1023
#define ANALOG_RGB_GAMMA        2.2f

#define ANALOG_RGB_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define ANALOG_RGB_TIMER        LEDC_TIMER_0

#define ANALOG_RGB_MAX_CHANNELS 5       // R, G, B, W1, W2

struct analog_rgb_t {
    int count;                          // channels in use (3-5)
    int gpio[ANALOG_RGB_MAX_CHANNELS];  // R, G, B, W1, W2
    ledc_channel_t channel[ANALOG_RGB_MAX_CHANNELS];
    uint8_t level[ANALOG_RGB_MAX_CHANNELS];  // staged levels
};

// Perceptual (gamma) lookup so 50% brightness looks half as bright and low
// levels have fine steps; built on first use.
static uint16_t s_gamma_table[256];
static bool s_gamma_ready = false;

static void build_gamma_table(void)
{
    for (int i = 0; i < 256; i++) {
        float v = powf((float)i / 255.0f, ANALOG_RGB_GAMMA);
        uint16_t duty = (uint16_t)(v * ANALOG_RGB_MAX_DUTY + 0.5f);
        // Any non-zero request produces at least the smallest duty step, so the
        // table stays monotonic and a dim request never reads as "off".
        s_gamma_table[i] = (i > 0 && duty == 0) ? 1 : duty;
    }
    s_gamma_ready = true;
}

uint32_t analog_rgb_level_to_duty(uint8_t level)
{
    if (!s_gamma_ready) {
        build_gamma_table();
    }
    return s_gamma_table[level];
}

esp_err_t analog_rgb_new(const analog_rgb_config_t *config, analog_rgb_handle_t *ret)
{
    ESP_RETURN_ON_FALSE(config != NULL && ret != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->gpio_w1 >= 0 || config->gpio_w2 < 0, ESP_ERR_INVALID_ARG, TAG,
                        "a second white pin needs a first white pin");

    int pins[ANALOG_RGB_MAX_CHANNELS] = {
        config->gpio_r, config->gpio_g, config->gpio_b, config->gpio_w1, config->gpio_w2,
    };
    int count = (config->gpio_w2 >= 0) ? 5 : (config->gpio_w1 >= 0) ? 4 : 3;
    for (int i = 0; i < count; i++) {
        ESP_RETURN_ON_FALSE(pins[i] >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid pin");
        for (int j = i + 1; j < count; j++) {
            ESP_RETURN_ON_FALSE(pins[i] != pins[j], ESP_ERR_INVALID_ARG, TAG,
                                "channel pins must all be different");
        }
    }

    struct analog_rgb_t *dev = calloc(1, sizeof(struct analog_rgb_t));
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_NO_MEM, TAG, "no mem");

    dev->count = count;
    for (int i = 0; i < count; i++) {
        dev->gpio[i] = pins[i];
        dev->channel[i] = (ledc_channel_t)(LEDC_CHANNEL_0 + i);
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = ANALOG_RGB_SPEED_MODE,
        .duty_resolution = ANALOG_RGB_RESOLUTION,
        .timer_num = ANALOG_RGB_TIMER,
        .freq_hz = ANALOG_RGB_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
        free(dev);
        return err;
    }

    for (int i = 0; i < dev->count; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num = dev->gpio[i],
            .speed_mode = ANALOG_RGB_SPEED_MODE,
            .channel = dev->channel[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = ANALOG_RGB_TIMER,
            .duty = 0,      // start with the light off
            .hpoint = 0,
        };
        err = ledc_channel_config(&ch_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel %d (GPIO%d) config failed: %s",
                     i, dev->gpio[i], esp_err_to_name(err));
            analog_rgb_del(dev);
            return err;
        }
    }

    ESP_LOGI(TAG, "Analog output: R=GPIO%d G=GPIO%d B=GPIO%d W1=%d W2=%d (-1 = none) (%d Hz, %d-bit, gamma %.1f)",
             dev->gpio[0], dev->gpio[1], dev->gpio[2],
             count > 3 ? dev->gpio[3] : -1, count > 4 ? dev->gpio[4] : -1,
             ANALOG_RGB_FREQ_HZ, 10, (double)ANALOG_RGB_GAMMA);

    *ret = dev;
    return ESP_OK;
}

esp_err_t analog_rgb_set(analog_rgb_handle_t handle, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t w1, uint8_t w2)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    handle->level[0] = r;
    handle->level[1] = g;
    handle->level[2] = b;
    handle->level[3] = w1;
    handle->level[4] = w2;
    return ESP_OK;
}

int analog_rgb_channel_count(analog_rgb_handle_t handle)
{
    return handle ? handle->count : 0;
}

esp_err_t analog_rgb_refresh(analog_rgb_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    esp_err_t result = ESP_OK;
    for (int i = 0; i < handle->count; i++) {
        esp_err_t err = ledc_set_duty(ANALOG_RGB_SPEED_MODE, handle->channel[i],
                                      analog_rgb_level_to_duty(handle->level[i]));
        if (err == ESP_OK) {
            err = ledc_update_duty(ANALOG_RGB_SPEED_MODE, handle->channel[i]);
        }
        if (err != ESP_OK) {
            result = err;
        }
    }
    return result;
}

esp_err_t analog_rgb_clear(analog_rgb_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    analog_rgb_set(handle, 0, 0, 0, 0, 0);
    return analog_rgb_refresh(handle);
}

esp_err_t analog_rgb_del(analog_rgb_handle_t handle)
{
    if (handle == NULL) {
        return ESP_OK;
    }
    for (int i = 0; i < handle->count; i++) {
        // idle_level 0: leave the MOSFET gates low (lamp off)
        ledc_stop(ANALOG_RGB_SPEED_MODE, handle->channel[i], 0);
    }
    free(handle);
    return ESP_OK;
}
