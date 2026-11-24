/*
 * TLED - Matter-over-Thread LED Controller
 * Analog RGB (PWM) output driver
 *
 * Drives a plain analog light (common-positive LEDs switched by one low-side
 * MOSFET per channel, e.g. a lamp driver board) with LEDC PWM, one GPIO per
 * channel: R, G, B plus up to two optional white channels (W1, W2). Every
 * "pixel" is the same colour: there is no per-LED addressing on an analog light.
 *
 * Output is active-high: a GPIO at 100% duty turns the MOSFET fully on.
 */

#pragma once

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct analog_rgb_t *analog_rgb_handle_t;

typedef struct {
    int gpio_r;     // GPIO driving the red channel MOSFET gate
    int gpio_g;     // GPIO driving the green channel MOSFET gate
    int gpio_b;     // GPIO driving the blue channel MOSFET gate
    int gpio_w1;    // First white channel GPIO, or -1 if the light has none
    int gpio_w2;    // Second white channel GPIO, or -1 (only valid with gpio_w1)
} analog_rgb_config_t;

/**
 * @brief Create the PWM outputs (all channels start off)
 */
esp_err_t analog_rgb_new(const analog_rgb_config_t *config, analog_rgb_handle_t *ret);

/**
 * @brief Stage the colour (0-255 per channel, linear light level).
 *        w1/w2 are ignored for channels the light doesn't have.
 *        Takes effect on analog_rgb_refresh().
 */
esp_err_t analog_rgb_set(analog_rgb_handle_t handle, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t w1, uint8_t w2);

/**
 * @brief Number of channels in use (3, 4 or 5)
 */
int analog_rgb_channel_count(analog_rgb_handle_t handle);

/**
 * @brief Apply the staged colour to the PWM outputs
 */
esp_err_t analog_rgb_refresh(analog_rgb_handle_t handle);

/**
 * @brief Turn all channels off
 */
esp_err_t analog_rgb_clear(analog_rgb_handle_t handle);

/**
 * @brief Stop the outputs and free the handle
 */
esp_err_t analog_rgb_del(analog_rgb_handle_t handle);

/**
 * @brief Convert an 8-bit level to a PWM duty (gamma-corrected). Exposed for tests.
 */
uint32_t analog_rgb_level_to_duty(uint8_t level);

#ifdef __cplusplus
}
#endif
