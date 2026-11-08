/*
 * TLED - Matter-over-Thread LED Controller
 * WS2805 5-channel (RGB + WW + CW) strip driver interface
 *
 * The WS2805 uses the same single-wire NRZ protocol as the WS2812 family,
 * but carries 40 bits per IC in the order G, R, B, W1, W2 (high bit first) -
 * the same GRB convention as WS2812B (WLED's NeoGrbwwFeature).
 * The espressif/led_strip component only supports 3/4-byte pixels, so this
 * standalone RMT-based driver handles the 5-byte format.
 *
 * Note on addressability: one WS2805 IC drives a group of LEDs (e.g. 6 LEDs
 * on 24V strips), so num_pixels here is the number of ICs, not LEDs.
 *
 * The chip has a backup data input (DIN2/BIN) that carries the same signal
 * as DIN so a single dead IC doesn't break the chain. This driver can
 * optionally mirror the DIN waveform to a second GPIO via the GPIO matrix -
 * no extra peripheral needed.
 */

#pragma once

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a WS2805 strip
 */
typedef struct ws2805_strip_t *ws2805_strip_handle_t;

/**
 * @brief WS2805 strip configuration
 */
typedef struct {
    int gpio_num;           // GPIO connected to DIN (primary data)
    int bin_gpio_num;       // GPIO connected to DIN2/BIN (backup data), -1 to disable
    uint32_t num_pixels;    // Number of WS2805 ICs (one IC = one addressable group)
} ws2805_strip_config_t;

/**
 * @brief Create a WS2805 strip driver on an RMT TX channel
 *
 * @param config Strip configuration
 * @param ret_strip Returned strip handle
 * @return ESP_OK on success
 */
esp_err_t ws2805_strip_new(const ws2805_strip_config_t *config, ws2805_strip_handle_t *ret_strip);

/**
 * @brief Set one pixel (IC group) in the frame buffer
 *
 * Values are raw wire-order channels: callers handle any RGB order remapping
 * and warm/cool white swapping before calling.
 *
 * @param strip Strip handle
 * @param index Pixel index (0-based)
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @param w1 White 1 (0-255)
 * @param w2 White 2 (0-255)
 * @return ESP_OK on success
 */
esp_err_t ws2805_strip_set_pixel(ws2805_strip_handle_t strip, uint32_t index,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t w1, uint8_t w2);

/**
 * @brief Transmit the frame buffer to the strip (blocks until done)
 *
 * @param strip Strip handle
 * @return ESP_OK on success
 */
esp_err_t ws2805_strip_refresh(ws2805_strip_handle_t strip);

/**
 * @brief Clear the frame buffer and turn all channels off
 *
 * @param strip Strip handle
 * @return ESP_OK on success
 */
esp_err_t ws2805_strip_clear(ws2805_strip_handle_t strip);

/**
 * @brief Delete the strip driver and release the RMT channel
 *
 * @param strip Strip handle
 * @return ESP_OK on success
 */
esp_err_t ws2805_strip_del(ws2805_strip_handle_t strip);

#ifdef __cplusplus
}
#endif
