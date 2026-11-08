/*
 * TLED - Matter-over-Thread LED Controller
 * WS2805 5-channel (RGB + WW + CW) strip driver implementation
 *
 * Uses the standard ESP-IDF RMT "bytes encoder + copy encoder" pattern
 * (same as the led_strip component) with WS2805 timings from the Worldsemi
 * datasheet (v0.3):
 *   T0H 220-380ns, T1H 580ns-1us, T0L/T1L 580ns-1us,
 *   bit period >= 1.25us, reset (RES) >= 280us low.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>  // __containerof

#include <esp_log.h>
#include <esp_check.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_struct.h>
#include <freertos/FreeRTOS.h>

#include "ws2805_strip.h"

static const char *TAG = "ws2805";

// RMT resolution: 10MHz = 100ns per tick (matches the led_strip RMT config)
#define WS2805_RMT_RESOLUTION_HZ (10 * 1000 * 1000)

// Bit timings in RMT ticks (100ns each). Both bits are 1.3us total,
// satisfying the >= 1.25us bit period from the datasheet.
#define WS2805_T0H_TICKS 3   // 300ns  (datasheet: 220-380ns)
#define WS2805_T0L_TICKS 10  // 1000ns (datasheet: 580ns-1us)
#define WS2805_T1H_TICKS 7   // 700ns  (datasheet: 580ns-1us)
#define WS2805_T1L_TICKS 6   // 600ns  (datasheet: 580ns-1us)

// Reset code: 300us low (datasheet requires >= 280us), split across the
// two halves of one RMT symbol.
#define WS2805_RESET_TICKS (WS2805_RMT_RESOLUTION_HZ / 1000000 * 300 / 2)

#define WS2805_BYTES_PER_PIXEL 5

// Custom RMT encoder: streams the pixel bytes, then appends the reset code
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;                      // 0 = sending pixels, 1 = sending reset
    rmt_symbol_word_t reset_code;
} ws2805_encoder_t;

struct ws2805_strip_t {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    uint32_t num_pixels;
    uint8_t *buffer;                // num_pixels * 5 bytes, wire order G,R,B,W1,W2
};

static size_t ws2805_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    ws2805_encoder_t *ws_encoder = __containerof(encoder, ws2805_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = ws_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = ws_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (ws_encoder->state) {
    case 0: // send pixel data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel,
                                                 primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            ws_encoder->state = 1; // pixels done, move on to reset code
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            break; // yield, resume in next callback
        }
    // fall-through
    case 1: // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel,
                                                &ws_encoder->reset_code,
                                                sizeof(ws_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            ws_encoder->state = 0; // back to the initial state for the next frame
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
        }
        break;
    }

    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t ws2805_encoder_reset(rmt_encoder_t *encoder)
{
    ws2805_encoder_t *ws_encoder = __containerof(encoder, ws2805_encoder_t, base);
    rmt_encoder_reset(ws_encoder->bytes_encoder);
    rmt_encoder_reset(ws_encoder->copy_encoder);
    ws_encoder->state = 0;
    return ESP_OK;
}

static esp_err_t ws2805_encoder_del(rmt_encoder_t *encoder)
{
    ws2805_encoder_t *ws_encoder = __containerof(encoder, ws2805_encoder_t, base);
    if (ws_encoder->bytes_encoder) {
        rmt_del_encoder(ws_encoder->bytes_encoder);
    }
    if (ws_encoder->copy_encoder) {
        rmt_del_encoder(ws_encoder->copy_encoder);
    }
    free(ws_encoder);
    return ESP_OK;
}

static esp_err_t ws2805_new_encoder(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t err = ESP_OK;

    ws2805_encoder_t *ws_encoder = calloc(1, sizeof(ws2805_encoder_t));
    ESP_RETURN_ON_FALSE(ws_encoder != NULL, ESP_ERR_NO_MEM, TAG, "no mem for encoder");

    ws_encoder->base.encode = ws2805_encode;
    ws_encoder->base.reset = ws2805_encoder_reset;
    ws_encoder->base.del = ws2805_encoder_del;

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2805_T0H_TICKS,
            .level1 = 0,
            .duration1 = WS2805_T0L_TICKS,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = WS2805_T1H_TICKS,
            .level1 = 0,
            .duration1 = WS2805_T1L_TICKS,
        },
        .flags = { .msb_first = 1 }, // datasheet: high bit is sent first
    };
    err = rmt_new_bytes_encoder(&bytes_encoder_config, &ws_encoder->bytes_encoder);
    if (err != ESP_OK) {
        goto cleanup;
    }

    rmt_copy_encoder_config_t copy_encoder_config = {};
    err = rmt_new_copy_encoder(&copy_encoder_config, &ws_encoder->copy_encoder);
    if (err != ESP_OK) {
        goto cleanup;
    }

    ws_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = WS2805_RESET_TICKS,
        .level1 = 0,
        .duration1 = WS2805_RESET_TICKS,
    };

    *ret_encoder = &ws_encoder->base;
    return ESP_OK;

cleanup:
    ws2805_encoder_del(&ws_encoder->base);
    return err;
}

// Mirror the DIN waveform to the BIN (DIN2 backup) pin via the GPIO matrix.
// The RMT driver routed its TX signal to din_gpio; read that signal index
// back from the GPIO matrix and route the same signal to bin_gpio, so both
// pins output bit-identical waveforms from a single RMT channel.
static esp_err_t ws2805_mirror_din_to_bin(int din_gpio, int bin_gpio)
{
    uint32_t signal = GPIO.func_out_sel_cfg[din_gpio].out_sel;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << bin_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    esp_rom_gpio_connect_out_signal(bin_gpio, signal, false, false);
    ESP_LOGI(TAG, "Mirroring DIN (GPIO%d, signal %lu) to BIN (GPIO%d)",
             din_gpio, (unsigned long)signal, bin_gpio);
    return ESP_OK;
}

esp_err_t ws2805_strip_new(const ws2805_strip_config_t *config, ws2805_strip_handle_t *ret_strip)
{
    ESP_RETURN_ON_FALSE(config != NULL && ret_strip != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->num_pixels > 0, ESP_ERR_INVALID_ARG, TAG, "num_pixels must be > 0");

    esp_err_t err = ESP_OK;

    struct ws2805_strip_t *strip = calloc(1, sizeof(struct ws2805_strip_t));
    ESP_RETURN_ON_FALSE(strip != NULL, ESP_ERR_NO_MEM, TAG, "no mem for strip");

    strip->num_pixels = config->num_pixels;
    strip->buffer = calloc(config->num_pixels, WS2805_BYTES_PER_PIXEL);
    if (strip->buffer == NULL) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    rmt_tx_channel_config_t tx_config = {
        .gpio_num = config->gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2805_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = { .with_dma = false },
    };
    err = rmt_new_tx_channel(&tx_config, &strip->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = ws2805_new_encoder(&strip->encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create encoder: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = rmt_enable(strip->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Mirror to the backup data line if configured (must happen after
    // rmt_new_tx_channel has routed the TX signal to the DIN gpio)
    if (config->bin_gpio_num >= 0) {
        err = ws2805_mirror_din_to_bin(config->gpio_num, config->bin_gpio_num);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to mirror to BIN GPIO%d: %s (continuing with DIN only)",
                     config->bin_gpio_num, esp_err_to_name(err));
            err = ESP_OK; // non-fatal: strip still works via DIN
        }
    }

    ESP_LOGI(TAG, "WS2805 strip created: %lu ICs on GPIO%d%s",
             (unsigned long)config->num_pixels, config->gpio_num,
             config->bin_gpio_num >= 0 ? " (+BIN mirror)" : "");

    *ret_strip = strip;
    return ESP_OK;

cleanup:
    ws2805_strip_del(strip);
    return err;
}

esp_err_t ws2805_strip_set_pixel(ws2805_strip_handle_t strip, uint32_t index,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t w1, uint8_t w2)
{
    ESP_RETURN_ON_FALSE(strip != NULL, ESP_ERR_INVALID_ARG, TAG, "strip is NULL");
    ESP_RETURN_ON_FALSE(index < strip->num_pixels, ESP_ERR_INVALID_ARG, TAG, "index out of range");

    // Wire order matches WS2812B convention (and WLED's NeoGrbwwFeature): G, R, B, W1, W2.
    // Callers pass logical (r, g, b) after any RGB-order remapping; this function applies
    // the final GRB swap so RGB_ORDER_GRB (the default) means "GRB on the wire".
    uint8_t *pixel = &strip->buffer[index * WS2805_BYTES_PER_PIXEL];
    pixel[0] = g;
    pixel[1] = r;
    pixel[2] = b;
    pixel[3] = w1;
    pixel[4] = w2;
    return ESP_OK;
}

esp_err_t ws2805_strip_refresh(ws2805_strip_handle_t strip)
{
    ESP_RETURN_ON_FALSE(strip != NULL, ESP_ERR_INVALID_ARG, TAG, "strip is NULL");

    // Single-shot transmission: send one frame and block until done (including the
    // 300µs reset code). This matches the led_strip component's behaviour and avoids
    // the race where the RMT reads the buffer while the CPU is writing to it.
    // WS2805 ICs hold their last latched colour between frames, so continuous looping
    // is not required.
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(strip->channel, strip->encoder, strip->buffer,
                                 strip->num_pixels * WS2805_BYTES_PER_PIXEL, &tx_config);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(strip->channel, portMAX_DELAY);
    }
    return err;
}

esp_err_t ws2805_strip_clear(ws2805_strip_handle_t strip)
{
    ESP_RETURN_ON_FALSE(strip != NULL, ESP_ERR_INVALID_ARG, TAG, "strip is NULL");

    memset(strip->buffer, 0, strip->num_pixels * WS2805_BYTES_PER_PIXEL);
    return ws2805_strip_refresh(strip);
}

esp_err_t ws2805_strip_del(ws2805_strip_handle_t strip)
{
    if (strip == NULL) {
        return ESP_OK;
    }
    if (strip->channel) {
        rmt_tx_wait_all_done(strip->channel, pdMS_TO_TICKS(100));
        rmt_disable(strip->channel);
        rmt_del_channel(strip->channel);
    }
    if (strip->encoder) {
        rmt_del_encoder(strip->encoder);
    }
    free(strip->buffer);
    free(strip);
    return ESP_OK;
}
