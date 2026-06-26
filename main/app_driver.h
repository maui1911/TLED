/*
 * TLED - Matter-over-Thread LED Controller
 * LED Driver Interface
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include "app_nvs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for the driver
 */
typedef void *app_driver_handle_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
} tled_rgbw_color_t;

/**
 * @brief Convert logical RGB to RGBW using the configured SK6812 white mode.
 *
 * Pure helper for the output pipeline and unit tests. Input and output channels
 * are 0-255. Gains are applied after white mixing.
 */
static inline uint8_t tled_apply_channel_gain(uint8_t value, uint8_t gain)
{
    return (uint8_t)(((uint16_t)value * gain) / 255);
}

static inline tled_rgbw_color_t tled_rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b,
                                                 uint8_t white_mode, uint8_t manual_white,
                                                 uint8_t gain_r, uint8_t gain_g,
                                                 uint8_t gain_b, uint8_t gain_w)
{
    uint8_t min_rgb = r < g ? r : g;
    min_rgb = min_rgb < b ? min_rgb : b;
    uint8_t max_rgb = r > g ? r : g;
    max_rgb = max_rgb > b ? max_rgb : b;

    uint8_t ro = r;
    uint8_t go = g;
    uint8_t bo = b;
    uint8_t wo = 0;

    switch (white_mode) {
        case WHITE_MODE_BRIGHTER:
            wo = min_rgb;
            break;
        case WHITE_MODE_NONE:
            wo = manual_white;
            break;
        case WHITE_MODE_DUAL:
            wo = manual_white > 0 ? manual_white : min_rgb;
            break;
        case WHITE_MODE_MAX:
            wo = max_rgb;
            break;
        case WHITE_MODE_ACCURATE:
        default:
            ro = r - min_rgb;
            go = g - min_rgb;
            bo = b - min_rgb;
            wo = min_rgb;
            break;
    }

    tled_rgbw_color_t out;
    out.r = tled_apply_channel_gain(ro, gain_r);
    out.g = tled_apply_channel_gain(go, gain_g);
    out.b = tled_apply_channel_gain(bo, gain_b);
    out.w = tled_apply_channel_gain(wo, gain_w);
    return out;
}

/**
 * @brief Initialize the LED driver (onboard LED for Phase 1)
 *
 * @return Driver handle, or NULL on failure
 */
app_driver_handle_t app_driver_light_init(void);

/**
 * @brief Initialize the button driver
 *
 * @return Driver handle, or NULL on failure
 */
app_driver_handle_t app_driver_button_init(void);

/**
 * @brief Set the LED power state
 *
 * @param handle Driver handle
 * @param power true = on, false = off
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_power(app_driver_handle_t handle, bool power);

/**
 * @brief Get the current LED power state
 *
 * @param handle Driver handle
 * @return true if LED is on, false if off
 */
bool app_driver_light_get_power(app_driver_handle_t handle);

/**
 * @brief Set brightness level
 *
 * @param handle Driver handle
 * @param brightness Brightness level (0-254 Matter range)
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_brightness(app_driver_handle_t handle, uint8_t brightness);

/**
 * @brief Set color using HSV
 *
 * @param handle Driver handle
 * @param hue Hue (0-254 Matter range)
 * @param saturation Saturation (0-254 Matter range)
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_hsv(app_driver_handle_t handle, uint8_t hue, uint8_t saturation);

/**
 * @brief Handle Matter attribute updates
 *
 * Called by the Matter stack when attributes change.
 *
 * @param driver_handle Driver handle
 * @param endpoint_id Matter endpoint ID
 * @param cluster_id Matter cluster ID
 * @param attribute_id Matter attribute ID
 * @param val New attribute value
 * @return ESP_OK on success
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                       uint16_t endpoint_id,
                                       uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *val);

/**
 * @brief Apply default settings to the light
 *
 * Reads current attribute values and applies them to hardware.
 *
 * @param endpoint_id Matter endpoint ID
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);

/**
 * @brief Set brightness with transition time
 *
 * @param handle Driver handle
 * @param brightness Target brightness (0-254 Matter range)
 * @param transition_ms Transition time in milliseconds (0 = instant)
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_brightness_with_transition(app_driver_handle_t handle,
                                                           uint8_t brightness,
                                                           uint32_t transition_ms);

/**
 * @brief Set HSV color with transition time
 *
 * @param handle Driver handle
 * @param hue Target hue (0-254 Matter range)
 * @param saturation Target saturation (0-254 Matter range)
 * @param transition_ms Transition time in milliseconds (0 = instant)
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_hsv_with_transition(app_driver_handle_t handle,
                                                    uint8_t hue,
                                                    uint8_t saturation,
                                                    uint32_t transition_ms);

/**
 * @brief Set color temperature with a custom transition time
 *
 * Converts the color temperature (in mireds) to an RGB white point and drives
 * it through the HSV transition engine. On SK6812 RGBW strips the white
 * component is routed to the dedicated W channel via the configured white mode.
 *
 * @param handle Driver handle
 * @param mireds Color temperature in mireds (1,000,000 / Kelvin)
 * @param transition_ms Transition duration in milliseconds (0 = immediate)
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_color_temperature_with_transition(app_driver_handle_t handle,
                                                                 uint16_t mireds,
                                                                 uint32_t transition_ms);

/**
 * @brief Set an effect mode
 *
 * @param handle Driver handle
 * @param effect_id Effect ID (see TLED_EFFECT_* in app_config.h)
 * @return ESP_OK on success
 */
esp_err_t app_driver_light_set_effect(app_driver_handle_t handle, uint8_t effect_id);

/**
 * @brief Get current effect mode
 *
 * @param handle Driver handle
 * @return Current effect ID
 */
uint8_t app_driver_light_get_effect(app_driver_handle_t handle);

#ifdef __cplusplus
}
#endif
