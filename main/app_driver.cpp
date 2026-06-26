/*
 * TLED - Matter-over-Thread LED Controller
 * LED Driver Implementation (Phase 3: Transitions & Effects)
 */

#include <esp_log.h>
#include <driver/gpio.h>
#include <led_strip.h>
#include <math.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

#include <esp_matter.h>
#include <platform/CHIPDeviceLayer.h>
#include "app_driver.h"
#include "app_config.h"
#include "app_nvs_config.h"

#include <iot_button.h>
#include <button_gpio.h>

// NVS namespace and keys for state persistence
#define NVS_NAMESPACE "tled_state"
#define NVS_KEY_POWER "power"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_HUE "hue"
#define NVS_KEY_SATURATION "saturation"
#define NVS_KEY_COLOR_MODE "color_mode"
#define NVS_KEY_CT_MIREDS "ct_mireds"

// Debounce timing for NVS saves (5 seconds)
#define NVS_SAVE_DEBOUNCE_MS 5000

// Debounce timing for HSV updates (50ms to combine rapid hue+sat changes)
#define HSV_UPDATE_DEBOUNCE_MS 50

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "tled_driver";

// External reference to light endpoint ID (defined in app_main.cpp)
extern uint16_t light_endpoint_id;

// Color modes
typedef enum {
    COLOR_MODE_HSV = 0,
    COLOR_MODE_XY = 1,
    COLOR_MODE_CT = 2,   // Color temperature (mireds)
} color_mode_t;

// Transition state
typedef struct {
    // Start values (captured when transition begins)
    float start_hue;
    float start_sat;
    float start_val;

    // Target values
    float target_hue;
    float target_sat;
    float target_val;

    // Current interpolated values (for display)
    float current_hue;
    float current_sat;
    float current_val;

    // Transition timing (using ticks for proper wrap-around handling)
    TickType_t transition_start_ticks;
    uint32_t transition_duration_ms;
    bool transitioning;
} transition_state_t;

// Driver state structure
typedef struct {
    led_strip_handle_t strip;
    bool is_rgbw;
    bool power;
    uint8_t brightness;     // 0-254 (Matter range)
    uint8_t hue;            // 0-254 (Matter range)
    uint8_t saturation;     // 0-254 (Matter range)
    uint16_t color_x;       // 0-65535 (Matter CIE x * 65535)
    uint16_t color_y;       // 0-65535 (Matter CIE y * 65535)
    uint16_t color_temp_mireds;  // Last color temperature (mireds)
    color_mode_t color_mode;

    // Phase 3: Transitions and effects
    transition_state_t transition;
    uint8_t effect_id;
    uint32_t effect_step;
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;

    // Phase 4: Runtime config
    uint16_t num_leds;
    uint8_t gpio_pin;
    uint8_t max_brightness;
    uint8_t rgb_order;      // RGB byte order (tled_rgb_order_t)
    uint8_t white_mode;     // RGBW white mixing mode (tled_white_mode_t)
    uint8_t manual_white;
    uint8_t gain_r;
    uint8_t gain_g;
    uint8_t gain_b;
    uint8_t gain_w;
    uint16_t white_led_kelvin;  // Native color temp of the W LED (K)

    // Debounce state for HSV updates (issue 7)
    bool hsv_update_pending;
    TickType_t hsv_update_time;

    // Debounce state for NVS saves (issue 8)
    bool nvs_save_pending;
    TimerHandle_t nvs_save_timer;
} light_driver_t;

// Static driver instance
static light_driver_t s_light_driver = {
    .strip = NULL,
    .is_rgbw = false,
    .power = false,
    .brightness = 127,
    .hue = 0,
    .saturation = 0,
    .color_x = 24939,
    .color_y = 24701,
    .color_temp_mireds = 250,
    .color_mode = COLOR_MODE_HSV,
    .transition = {0},
    .effect_id = TLED_EFFECT_NONE,
    .effect_step = 0,
    .task_handle = NULL,
    .mutex = NULL,
    .num_leds = TLED_DEFAULT_NUM_LEDS,
    .gpio_pin = TLED_DEFAULT_GPIO_PIN,
    .max_brightness = TLED_DEFAULT_MAX_BRIGHTNESS,
    .rgb_order = TLED_DEFAULT_RGB_ORDER,
    .white_mode = TLED_DEFAULT_WHITE_MODE,
    .manual_white = TLED_DEFAULT_MANUAL_WHITE,
    .gain_r = TLED_DEFAULT_CHANNEL_GAIN,
    .gain_g = TLED_DEFAULT_CHANNEL_GAIN,
    .gain_b = TLED_DEFAULT_CHANNEL_GAIN,
    .gain_w = TLED_DEFAULT_CHANNEL_GAIN,
    .white_led_kelvin = TLED_DEFAULT_WHITE_LED_KELVIN,
    .hsv_update_pending = false,
    .hsv_update_time = 0,
    .nvs_save_pending = false,
    .nvs_save_timer = NULL
};

// Forward declarations
static void transition_task(void *arg);
static esp_err_t update_strip_rgb(light_driver_t *driver, uint8_t r, uint8_t g, uint8_t b);
static void start_transition(light_driver_t *driver, uint8_t target_hue, uint8_t target_sat,
                             uint8_t target_val, uint32_t duration_ms);

// Actually perform the NVS save (called by timer or directly)
static esp_err_t do_save_state_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Check each nvs_set return value
    err = nvs_set_u8(handle, NVS_KEY_POWER, s_light_driver.power ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set power: %s", esp_err_to_name(err));
    }
    err = nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, s_light_driver.brightness);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set brightness: %s", esp_err_to_name(err));
    }
    err = nvs_set_u8(handle, NVS_KEY_HUE, s_light_driver.hue);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set hue: %s", esp_err_to_name(err));
    }
    err = nvs_set_u8(handle, NVS_KEY_SATURATION, s_light_driver.saturation);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set saturation: %s", esp_err_to_name(err));
    }
    err = nvs_set_u8(handle, NVS_KEY_COLOR_MODE, (uint8_t)s_light_driver.color_mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set color_mode: %s", esp_err_to_name(err));
    }
    err = nvs_set_u16(handle, NVS_KEY_CT_MIREDS, s_light_driver.color_temp_mireds);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set ct_mireds: %s", esp_err_to_name(err));
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    s_light_driver.nvs_save_pending = false;

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "State saved to NVS: power=%d, brightness=%d, hue=%d, sat=%d",
                 s_light_driver.power, s_light_driver.brightness,
                 s_light_driver.hue, s_light_driver.saturation);
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }
    return err;
}

// Timer callback for debounced NVS save
static void nvs_save_timer_callback(TimerHandle_t timer)
{
    ESP_LOGD(TAG, "NVS save timer fired");
    do_save_state_to_nvs();
}

// Schedule a debounced save to NVS (waits for stability before writing)
static void schedule_save_state_to_nvs(void)
{
    s_light_driver.nvs_save_pending = true;

    if (s_light_driver.nvs_save_timer != NULL) {
        // Reset the timer - this restarts the debounce period
        if (xTimerReset(s_light_driver.nvs_save_timer, 0) != pdPASS) {
            ESP_LOGW(TAG, "Failed to reset NVS save timer, saving immediately");
            do_save_state_to_nvs();
        }
    } else {
        // Timer not created yet (during init), save immediately
        do_save_state_to_nvs();
    }
}

// Load state from NVS
static esp_err_t load_state_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved state found, using defaults");
        return err;
    }

    uint8_t val;
    if (nvs_get_u8(handle, NVS_KEY_POWER, &val) == ESP_OK) {
        s_light_driver.power = (val != 0);
    }
    if (nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &val) == ESP_OK) {
        s_light_driver.brightness = val;
    }
    if (nvs_get_u8(handle, NVS_KEY_HUE, &val) == ESP_OK) {
        s_light_driver.hue = val;
    }
    if (nvs_get_u8(handle, NVS_KEY_SATURATION, &val) == ESP_OK) {
        s_light_driver.saturation = val;
    }
    if (nvs_get_u8(handle, NVS_KEY_COLOR_MODE, &val) == ESP_OK) {
        s_light_driver.color_mode = (color_mode_t)val;
    }
    uint16_t mireds;
    if (nvs_get_u16(handle, NVS_KEY_CT_MIREDS, &mireds) == ESP_OK) {
        s_light_driver.color_temp_mireds = mireds;
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "State loaded: power=%d, brightness=%d, hue=%d, sat=%d",
             s_light_driver.power, s_light_driver.brightness,
             s_light_driver.hue, s_light_driver.saturation);
    return ESP_OK;
}

// Convert HSV to RGB
// h: 0-360, s: 0-100, v: 0-100
// Output r, g, b: 0-255
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) {
        // Achromatic (grey)
        uint8_t val = (v * 255) / 100;
        *r = *g = *b = val;
        return;
    }

    // Ensure hue wraps around
    h = h % 360;

    uint16_t region = h / 60;
    uint16_t remainder = (h - (region * 60)) * 6;

    uint8_t p = (v * (100 - s)) * 255 / 10000;
    uint8_t q = (v * (100 - (s * remainder) / 360)) * 255 / 10000;
    uint8_t t = (v * (100 - (s * (360 - remainder)) / 360)) * 255 / 10000;
    uint8_t val = (v * 255) / 100;

    switch (region) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break;
    }
}

// Convert a color temperature (in mireds) to an approximate sRGB white point.
// mireds = 1,000,000 / Kelvin. Uses the Tanner Helland approximation, which is
// cheap and good enough for an LED strip. Output r,g,b: 0-255 at full value.
// On an SK6812 the downstream rgb_to_rgbw step routes the common (white)
// component to the dedicated W channel, so warm/cool white lands mostly on W.
static void ct_mireds_to_rgb(uint16_t mireds, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (mireds == 0) {
        mireds = 250;  // ~4000K fallback
    }
    // Kelvin from mireds, clamped to the algorithm's valid 1000-40000K range.
    float kelvin = 1000000.0f / (float)mireds;
    if (kelvin < 1000.0f) kelvin = 1000.0f;
    if (kelvin > 40000.0f) kelvin = 40000.0f;
    float temp = kelvin / 100.0f;

    float rf, gf, bf;

    // Red
    if (temp <= 66.0f) {
        rf = 255.0f;
    } else {
        rf = 329.698727446f * powf(temp - 60.0f, -0.1332047592f);
    }

    // Green
    if (temp <= 66.0f) {
        gf = 99.4708025861f * logf(temp) - 161.1195681661f;
    } else {
        gf = 288.1221695283f * powf(temp - 60.0f, -0.0755148492f);
    }

    // Blue
    if (temp >= 66.0f) {
        bf = 255.0f;
    } else if (temp <= 19.0f) {
        bf = 0.0f;
    } else {
        bf = 138.5177312231f * logf(temp - 10.0f) - 305.0447927307f;
    }

    // Clamp to [0, 255]
    rf = rf < 0 ? 0 : (rf > 255 ? 255 : rf);
    gf = gf < 0 ? 0 : (gf > 255 ? 255 : gf);
    bf = bf < 0 ? 0 : (bf > 255 ? 255 : bf);

    *r = (uint8_t)rf;
    *g = (uint8_t)gf;
    *b = (uint8_t)bf;
}

// Convert a target color temperature to RGBW, driving the dedicated white LED
// as the PRIMARY source rather than as a leftover RGB minimum.
//
// The W LED is not a pure white emitter: it has its own native color temp
// (white_led_kelvin). We model its emission as the RGB white point of that
// native temp, then find the largest amount of W (scalar k) that fits "under"
// the target white point in every channel. RGB then fills only the remainder.
// Result: when the target matches the strip's white LED, output is pure W;
// as the target diverges, RGB is added to steer the color, but W stays the
// dominant, highest-CRI, most efficient source. Output is 0-255 at full level.
static void ct_to_rgbw_full(uint16_t target_mireds, uint16_t white_led_kelvin,
                            uint8_t *ro, uint8_t *go, uint8_t *bo, uint8_t *wo)
{
    uint8_t tr, tg, tb;  // target white point
    ct_mireds_to_rgb(target_mireds, &tr, &tg, &tb);

    uint16_t native_mireds = white_led_kelvin ? (uint16_t)(1000000u / white_led_kelvin) : 250;
    uint8_t nr, ng, nb;  // emission color of the W LED at full
    ct_mireds_to_rgb(native_mireds, &nr, &ng, &nb);

    // k = max fraction of native white usable without overshooting any channel.
    // Channels where the W LED emits ~0 don't constrain k (k*0 never overshoots).
    float k = 1.0f;
    if (nr > 0) { float kk = (float)tr / (float)nr; if (kk < k) k = kk; }
    if (ng > 0) { float kk = (float)tg / (float)ng; if (kk < k) k = kk; }
    if (nb > 0) { float kk = (float)tb / (float)nb; if (kk < k) k = kk; }
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;

    float rr = (float)tr - k * (float)nr;
    float gg = (float)tg - k * (float)ng;
    float bb = (float)tb - k * (float)nb;
    if (rr < 0) rr = 0;
    if (gg < 0) gg = 0;
    if (bb < 0) bb = 0;

    *ro = (uint8_t)rr;
    *go = (uint8_t)gg;
    *bo = (uint8_t)bb;
    *wo = (uint8_t)(k * 255.0f);
}

// Remap RGB values based on configured byte order.
// The led_strip component with LED_PIXEL_FORMAT_GRB always transmits bytes as [G, R, B].
// If the LED expects a different order, we pre-swap the values so the physical bytes match.
static void remap_rgb_order(uint8_t order, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    switch (order) {
        case RGB_ORDER_GRB: // Default - no remapping needed
            *out_r = r; *out_g = g; *out_b = b;
            break;
        case RGB_ORDER_RGB: // LED expects [R,G,B], component sends [G,R,B] → swap r,g
            *out_r = g; *out_g = r; *out_b = b;
            break;
        case RGB_ORDER_BRG: // LED expects [B,R,G], component sends [G,R,B] → g→b, r→r, b→g
            *out_r = r; *out_g = b; *out_b = g;
            break;
        case RGB_ORDER_RBG: // LED expects [R,B,G], component sends [G,R,B] → g→r, r→b, b→g
            *out_r = b; *out_g = r; *out_b = g;
            break;
        case RGB_ORDER_BGR: // LED expects [B,G,R], component sends [G,R,B] → g→b, r→g, b→r
            *out_r = g; *out_g = b; *out_b = r;
            break;
        case RGB_ORDER_GBR: // LED expects [G,B,R], component sends [G,R,B] → r→b, b→r
            *out_r = b; *out_g = g; *out_b = r;
            break;
        default:
            *out_r = r; *out_g = g; *out_b = b;
            break;
    }
}

// Set a single pixel, handling RGB order remapping and RGBW conversion
static esp_err_t driver_set_pixel(light_driver_t *driver, int index,
                                   uint8_t r, uint8_t g, uint8_t b)
{
    if (driver->is_rgbw) {
        tled_rgbw_color_t rgbw = tled_rgb_to_rgbw(r, g, b,
                                                  driver->white_mode,
                                                  driver->manual_white,
                                                  driver->gain_r,
                                                  driver->gain_g,
                                                  driver->gain_b,
                                                  driver->gain_w);
        uint8_t mr, mg, mb;
        remap_rgb_order(driver->rgb_order, rgbw.r, rgbw.g, rgbw.b, &mr, &mg, &mb);
        return led_strip_set_pixel_rgbw(driver->strip, index, mr, mg, mb, rgbw.w);
    }
    // Apply RGB order remapping
    uint8_t mr, mg, mb;
    remap_rgb_order(driver->rgb_order, r, g, b, &mr, &mg, &mb);
    return led_strip_set_pixel(driver->strip, index, mr, mg, mb);
}

// Update strip with specific RGB values
static esp_err_t update_strip_rgb(light_driver_t *driver, uint8_t r, uint8_t g, uint8_t b)
{
    if (driver->strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < driver->num_leds; i++) {
        driver_set_pixel(driver, i, r, g, b);
    }

    return led_strip_refresh(driver->strip);
}

// Update strip with explicit RGBW values (CT path - bypasses white extraction).
// The RGB bytes still go through the configured order remap; W is written raw.
static esp_err_t update_strip_rgbw(light_driver_t *driver,
                                   uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    if (driver->strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t mr, mg, mb;
    remap_rgb_order(driver->rgb_order, r, g, b, &mr, &mg, &mb);
    for (int i = 0; i < driver->num_leds; i++) {
        led_strip_set_pixel_rgbw(driver->strip, i, mr, mg, mb, w);
    }
    return led_strip_refresh(driver->strip);
}

// Render the strip in color-temperature mode at a given brightness (Matter 0-254).
// Drives the dedicated W channel as the primary white source; on a non-RGBW
// strip it falls back to the equivalent RGB white point.
static esp_err_t render_ct(light_driver_t *driver, uint8_t brightness)
{
    uint8_t r, g, b, w;

    if (driver->is_rgbw) {
        ct_to_rgbw_full(driver->color_temp_mireds, driver->white_led_kelvin, &r, &g, &b, &w);
    } else {
        // No white channel: just emit the target white point as RGB.
        ct_mireds_to_rgb(driver->color_temp_mireds, &r, &g, &b);
        w = 0;
    }

    // Scale by brightness (0-254 -> 0-1).
    uint16_t bri = brightness;
    r = (uint8_t)(((uint16_t)r * bri) / MATTER_BRIGHTNESS_MAX);
    g = (uint8_t)(((uint16_t)g * bri) / MATTER_BRIGHTNESS_MAX);
    b = (uint8_t)(((uint16_t)b * bri) / MATTER_BRIGHTNESS_MAX);
    w = (uint8_t)(((uint16_t)w * bri) / MATTER_BRIGHTNESS_MAX);

    // Apply max_brightness power limit to every channel (W included).
    if (driver->max_brightness < 255) {
        r = (uint8_t)(((uint16_t)r * driver->max_brightness) / 255);
        g = (uint8_t)(((uint16_t)g * driver->max_brightness) / 255);
        b = (uint8_t)(((uint16_t)b * driver->max_brightness) / 255);
        w = (uint8_t)(((uint16_t)w * driver->max_brightness) / 255);
    }

    // Apply per-channel calibration gains (same knobs as the RGBW mix path).
    r = tled_apply_channel_gain(r, driver->gain_r);
    g = tled_apply_channel_gain(g, driver->gain_g);
    b = tled_apply_channel_gain(b, driver->gain_b);
    w = tled_apply_channel_gain(w, driver->gain_w);

    if (driver->is_rgbw) {
        return update_strip_rgbw(driver, r, g, b, w);
    }
    return update_strip_rgb(driver, r, g, b);
}

// Apply max_brightness clamping to RGB values
static void apply_max_brightness(light_driver_t *driver, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (driver->max_brightness < 255) {
        // Scale RGB values by max_brightness/255
        *r = (uint8_t)(((uint16_t)*r * driver->max_brightness) / 255);
        *g = (uint8_t)(((uint16_t)*g * driver->max_brightness) / 255);
        *b = (uint8_t)(((uint16_t)*b * driver->max_brightness) / 255);
    }
}

// Calculate current interpolated RGB from transition state
static void get_interpolated_rgb(light_driver_t *driver, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // Convert current interpolated values to standard ranges
    uint16_t hue = (uint16_t)((driver->transition.current_hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX);
    uint8_t sat = (uint8_t)((driver->transition.current_sat * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX);
    uint8_t val = (uint8_t)((driver->transition.current_val * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX);

    hsv_to_rgb(hue, sat, val, r, g, b);

    // Apply max_brightness limit (issue 9)
    apply_max_brightness(driver, r, g, b);
}

// Linear interpolation with hue wrap-around handling
static float lerp_hue(float from, float to, float t)
{
    // Handle hue wrap-around (0 and 254 are adjacent on the color wheel)
    // Matter hue range is 0-254, so full cycle is 255 values
    const float HUE_RANGE = 255.0f;
    const float HALF_RANGE = HUE_RANGE / 2.0f;

    float diff = to - from;

    // If the difference is more than half the range, go the short way around
    if (diff > HALF_RANGE) {
        diff -= HUE_RANGE;
    } else if (diff < -HALF_RANGE) {
        diff += HUE_RANGE;
    }

    float result = from + diff * t;

    // Wrap result to valid range [0, 254]
    while (result < 0) result += HUE_RANGE;
    while (result >= HUE_RANGE) result -= HUE_RANGE;

    return result;
}

static float lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

// Rainbow effect - cycle through hues
static void effect_rainbow(light_driver_t *driver)
{
    if (driver->strip == NULL) return;

    // Use effect_step as hue offset
    uint16_t base_hue = driver->effect_step % 360;

    // Get brightness from current setting
    uint8_t val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;

    uint8_t r, g, b;
    hsv_to_rgb(base_hue, 100, val, &r, &g, &b);
    apply_max_brightness(driver, &r, &g, &b);
    update_strip_rgb(driver, r, g, b);

    driver->effect_step += 2;  // Speed of rainbow
}

// Breathing effect - pulse brightness
static void effect_breathing(light_driver_t *driver)
{
    if (driver->strip == NULL) return;

    // Sine wave for smooth breathing
    float phase = (float)(driver->effect_step % 360) * 3.14159f / 180.0f;
    float breath = (sinf(phase) + 1.0f) / 2.0f;  // 0-1 range

    // Scale to brightness
    uint8_t max_val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;
    uint8_t val = (uint8_t)(breath * max_val);

    // Use current hue/sat
    uint16_t hue = (driver->hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX;
    uint8_t sat = (driver->saturation * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX;

    uint8_t r, g, b;
    hsv_to_rgb(hue, sat, val, &r, &g, &b);
    apply_max_brightness(driver, &r, &g, &b);
    update_strip_rgb(driver, r, g, b);

    driver->effect_step += 3;  // Speed of breathing
}

// Candle flicker effect
static void effect_candle(light_driver_t *driver)
{
    if (driver->strip == NULL) return;

    // Random flicker with warm color
    uint8_t max_val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;

    // Random variation: 50-100% of max brightness
    uint8_t variation = (esp_random() % 50) + 50;
    uint8_t val = (max_val * variation) / 100;

    // Warm orange/yellow hue (around 30-40 degrees)
    uint16_t hue = 30 + (esp_random() % 15);

    uint8_t r, g, b;
    hsv_to_rgb(hue, 100, val, &r, &g, &b);
    apply_max_brightness(driver, &r, &g, &b);
    update_strip_rgb(driver, r, g, b);

    driver->effect_step++;
}

// Chase effect - moving dot
static void effect_chase(light_driver_t *driver)
{
    if (driver->strip == NULL) return;

    // Get current color
    uint16_t hue = (driver->hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX;
    uint8_t sat = (driver->saturation * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX;
    uint8_t val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;

    uint8_t r, g, b;
    hsv_to_rgb(hue, sat, val, &r, &g, &b);
    apply_max_brightness(driver, &r, &g, &b);

    // Current position
    int num_leds = driver->num_leds;
    int pos = driver->effect_step % num_leds;
    int trail = (pos - 1 + num_leds) % num_leds;

    // Set all LEDs
    for (int i = 0; i < num_leds; i++) {
        if (i == pos) {
            driver_set_pixel(driver, i, r, g, b);
        } else if (i == trail) {
            driver_set_pixel(driver, i, r / 3, g / 3, b / 3);
        } else {
            driver_set_pixel(driver, i, 0, 0, 0);
        }
    }

    led_strip_refresh(driver->strip);
    driver->effect_step++;
}

// Main transition/effect task
static void transition_task(void *arg)
{
    light_driver_t *driver = (light_driver_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        xSemaphoreTake(driver->mutex, portMAX_DELAY);

        // Check for pending HSV updates that have been stable for debounce period (issue 7)
        if (driver->hsv_update_pending) {
            TickType_t now = xTaskGetTickCount();
            uint32_t elapsed_ms = (uint32_t)(now - driver->hsv_update_time) * portTICK_PERIOD_MS;
            if (elapsed_ms >= HSV_UPDATE_DEBOUNCE_MS) {
                // Debounce period elapsed - start the transition
                driver->hsv_update_pending = false;
                uint8_t hue = driver->hue;
                uint8_t sat = driver->saturation;
                uint8_t bri = driver->brightness;
                xSemaphoreGive(driver->mutex);

                ESP_LOGI(TAG, "HSV debounce complete: H=%d S=%d, starting transition", hue, sat);
                start_transition(driver, hue, sat, bri, TLED_DEFAULT_TRANSITION_MS);

                xSemaphoreTake(driver->mutex, portMAX_DELAY);
            }
        }

        if (!driver->power) {
            // Light is off - check if we're fading out (transitioning to val=0)
            if (driver->transition.transitioning && driver->transition.target_val == 0) {
                // Let the fade-out transition continue - don't skip to black yet
                // Fall through to transition handling below
            } else {
                // No fade-out in progress - set black and wait
                update_strip_rgb(driver, 0, 0, 0);
                // Keep current_val synced with actual output so transitions start from 0
                driver->transition.current_val = 0;
                driver->transition.transitioning = false;
                xSemaphoreGive(driver->mutex);
                vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
                continue;
            }
        }

        // Check if we're running an effect
        if (driver->effect_id != TLED_EFFECT_NONE) {
            switch (driver->effect_id) {
                case TLED_EFFECT_RAINBOW:
                    effect_rainbow(driver);
                    break;
                case TLED_EFFECT_BREATHING:
                    effect_breathing(driver);
                    break;
                case TLED_EFFECT_CANDLE:
                    effect_candle(driver);
                    break;
                case TLED_EFFECT_CHASE:
                    effect_chase(driver);
                    break;
            }
            xSemaphoreGive(driver->mutex);
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TLED_TRANSITION_TICK_MS));
            continue;
        }

        // Handle transition
        if (driver->transition.transitioning) {
            // Use tick difference that handles wrap-around correctly (unsigned subtraction)
            TickType_t now_ticks = xTaskGetTickCount();
            uint32_t elapsed = (uint32_t)(now_ticks - driver->transition.transition_start_ticks) * portTICK_PERIOD_MS;

            if (elapsed >= driver->transition.transition_duration_ms) {
                // Transition complete
                driver->transition.current_hue = driver->transition.target_hue;
                driver->transition.current_sat = driver->transition.target_sat;
                driver->transition.current_val = driver->transition.target_val;
                driver->transition.transitioning = false;
                ESP_LOGI(TAG, "Transition complete");
            } else {
                // Linear interpolation from start to target
                float t = (float)elapsed / (float)driver->transition.transition_duration_ms;

                // Interpolate hue with wrap-around
                driver->transition.current_hue = lerp_hue(
                    driver->transition.start_hue,
                    driver->transition.target_hue,
                    t
                );
                driver->transition.current_sat = lerp(
                    driver->transition.start_sat,
                    driver->transition.target_sat,
                    t
                );
                driver->transition.current_val = lerp(
                    driver->transition.start_val,
                    driver->transition.target_val,
                    t
                );
            }

            // Update strip with interpolated values. In CT mode the color is
            // rendered from mireds directly; only the brightness (current_val)
            // is interpolated, so temperature changes apply cleanly while
            // brightness still fades.
            if (driver->color_mode == COLOR_MODE_CT) {
                render_ct(driver, (uint8_t)driver->transition.current_val);
            } else {
                uint8_t r, g, b;
                get_interpolated_rgb(driver, &r, &g, &b);
                update_strip_rgb(driver, r, g, b);
            }
        }

        xSemaphoreGive(driver->mutex);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TLED_TRANSITION_TICK_MS));
    }
}

// Start a new transition
static void start_transition(light_driver_t *driver, uint8_t target_hue, uint8_t target_sat,
                             uint8_t target_val, uint32_t duration_ms)
{
    xSemaphoreTake(driver->mutex, portMAX_DELAY);

    // Capture current position as start (use current interpolated values if mid-transition)
    if (driver->transition.transitioning) {
        // Already transitioning - start from current interpolated position
        driver->transition.start_hue = driver->transition.current_hue;
        driver->transition.start_sat = driver->transition.current_sat;
        driver->transition.start_val = driver->transition.current_val;
    } else {
        // Not transitioning - start from last known state
        driver->transition.start_hue = driver->transition.current_hue;
        driver->transition.start_sat = driver->transition.current_sat;
        driver->transition.start_val = driver->transition.current_val;
    }

    driver->transition.target_hue = target_hue;
    driver->transition.target_sat = target_sat;
    driver->transition.target_val = target_val;
    driver->transition.transition_start_ticks = xTaskGetTickCount();
    driver->transition.transition_duration_ms = duration_ms > 0 ? duration_ms : 1;
    driver->transition.transitioning = true;

    // Stop any running effect
    driver->effect_id = TLED_EFFECT_NONE;

    ESP_LOGI(TAG, "Transition: H=%.0f->%d S=%.0f->%d V=%.0f->%d over %lums",
             driver->transition.start_hue, target_hue,
             driver->transition.start_sat, target_sat,
             driver->transition.start_val, target_val,
             (unsigned long)duration_ms);

    xSemaphoreGive(driver->mutex);
}

// Immediate update (no transition)
static esp_err_t update_immediate(light_driver_t *driver)
{
    xSemaphoreTake(driver->mutex, portMAX_DELAY);

    // Color-temperature mode renders RGBW directly (white-primary), bypassing
    // the HSV path entirely.
    if (driver->power && driver->color_mode == COLOR_MODE_CT) {
        esp_err_t err = render_ct(driver, driver->brightness);
        driver->transition.current_hue = driver->hue;
        driver->transition.current_sat = driver->saturation;
        driver->transition.current_val = driver->brightness;
        driver->transition.start_hue = driver->hue;
        driver->transition.start_sat = driver->saturation;
        driver->transition.start_val = driver->brightness;
        driver->transition.transitioning = false;
        ESP_LOGI(TAG, "CT mode: %u mireds (%uK) bri=%d",
                 driver->color_temp_mireds,
                 (unsigned)(driver->color_temp_mireds ? 1000000u / driver->color_temp_mireds : 0),
                 driver->brightness);
        xSemaphoreGive(driver->mutex);
        return err;
    }

    uint8_t r, g, b;

    if (!driver->power) {
        r = g = b = 0;
        ESP_LOGI(TAG, "Strip OFF");
    } else {
        uint16_t hue = (driver->hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX;
        uint8_t sat = (driver->saturation * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX;
        uint8_t val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;
        hsv_to_rgb(hue, sat, val, &r, &g, &b);
        apply_max_brightness(driver, &r, &g, &b);
        ESP_LOGI(TAG, "HSV mode: H=%d S=%d V=%d -> R=%d G=%d B=%d (max_bri=%d)",
                 driver->hue, driver->saturation, driver->brightness, r, g, b, driver->max_brightness);
    }

    // Also update transition state to match
    driver->transition.current_hue = driver->hue;
    driver->transition.current_sat = driver->saturation;
    driver->transition.current_val = driver->power ? driver->brightness : 0;
    driver->transition.start_hue = driver->transition.current_hue;
    driver->transition.start_sat = driver->transition.current_sat;
    driver->transition.start_val = driver->transition.current_val;
    driver->transition.transitioning = false;

    esp_err_t err = update_strip_rgb(driver, r, g, b);

    xSemaphoreGive(driver->mutex);

    return err;
}

esp_err_t app_driver_light_set_power(app_driver_handle_t handle, bool power)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "LED power set to %s", power ? "ON" : "OFF");

    if (power) {
        // Set power ON first, then start fade-in from 0
        driver->power = power;
        driver->transition.current_val = 0;  // Ensure we start from black
        start_transition(driver, driver->hue, driver->saturation, driver->brightness,
                        TLED_DEFAULT_TRANSITION_MS);
    } else {
        // Start fade-out BEFORE setting power off to avoid race with transition task
        start_transition(driver, driver->hue, driver->saturation, 0,
                        TLED_DEFAULT_TRANSITION_MS);
        driver->power = power;  // Set power off after transition is started
    }

    schedule_save_state_to_nvs();

    return ESP_OK;
}

bool app_driver_light_get_power(app_driver_handle_t handle)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return false;
    }
    // Single-byte read is atomic on ESP32, but use mutex for consistency if available
    if (driver->mutex != NULL) {
        xSemaphoreTake(driver->mutex, portMAX_DELAY);
        bool power = driver->power;
        xSemaphoreGive(driver->mutex);
        return power;
    }
    return driver->power;
}

esp_err_t app_driver_light_set_brightness(app_driver_handle_t handle, uint8_t brightness)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    driver->brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d", brightness);

    schedule_save_state_to_nvs();

    // Use default transition
    start_transition(driver, driver->hue, driver->saturation, brightness,
                    TLED_DEFAULT_TRANSITION_MS);

    return ESP_OK;
}

esp_err_t app_driver_light_set_brightness_with_transition(app_driver_handle_t handle,
                                                           uint8_t brightness,
                                                           uint32_t transition_ms)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    driver->brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d (transition %lums)", brightness, (unsigned long)transition_ms);

    schedule_save_state_to_nvs();

    if (transition_ms == 0) {
        return update_immediate(driver);
    }

    start_transition(driver, driver->hue, driver->saturation, brightness, transition_ms);
    return ESP_OK;
}

esp_err_t app_driver_light_set_hsv(app_driver_handle_t handle, uint8_t hue, uint8_t saturation)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    driver->hue = hue;
    driver->saturation = saturation;
    driver->color_mode = COLOR_MODE_HSV;
    ESP_LOGI(TAG, "Color set to H=%d S=%d", hue, saturation);

    schedule_save_state_to_nvs();

    // Use default transition
    start_transition(driver, hue, saturation, driver->brightness,
                    TLED_DEFAULT_TRANSITION_MS);

    return ESP_OK;
}

esp_err_t app_driver_light_set_hsv_with_transition(app_driver_handle_t handle,
                                                    uint8_t hue,
                                                    uint8_t saturation,
                                                    uint32_t transition_ms)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    driver->hue = hue;
    driver->saturation = saturation;
    driver->color_mode = COLOR_MODE_HSV;
    ESP_LOGI(TAG, "Color set to H=%d S=%d (transition %lums)", hue, saturation, (unsigned long)transition_ms);

    schedule_save_state_to_nvs();

    if (transition_ms == 0) {
        return update_immediate(driver);
    }

    start_transition(driver, hue, saturation, driver->brightness, transition_ms);
    return ESP_OK;
}

esp_err_t app_driver_light_set_color_temperature_with_transition(app_driver_handle_t handle,
                                                                 uint16_t mireds,
                                                                 uint32_t transition_ms)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // CT renders RGBW directly (white-primary) - no HSV round-trip. We store
    // the target mireds and switch into CT mode; the render path computes the
    // RGBW mix every frame. The transition only fades brightness, so a pure
    // temperature change applies immediately while brightness still ramps.
    driver->color_temp_mireds = mireds;
    driver->color_mode = COLOR_MODE_CT;
    ESP_LOGI(TAG, "Color temp set to %u mireds (%uK), transition %lums",
             mireds, (unsigned)(mireds ? 1000000u / mireds : 0), (unsigned long)transition_ms);

    schedule_save_state_to_nvs();

    if (transition_ms == 0) {
        return update_immediate(driver);
    }

    // Drive a transition (brightness fade); CT color is rendered from mireds.
    start_transition(driver, driver->hue, driver->saturation, driver->brightness, transition_ms);
    return ESP_OK;
}

esp_err_t app_driver_light_set_effect(app_driver_handle_t handle, uint8_t effect_id)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(driver->mutex, portMAX_DELAY);

    driver->effect_id = effect_id;
    driver->effect_step = 0;
    driver->transition.transitioning = false;

    ESP_LOGI(TAG, "Effect set to %d", effect_id);

    xSemaphoreGive(driver->mutex);

    return ESP_OK;
}

uint8_t app_driver_light_get_effect(app_driver_handle_t handle)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return TLED_EFFECT_NONE;
    }
    // Single-byte read is atomic on ESP32, but use mutex for consistency if available
    if (driver->mutex != NULL) {
        xSemaphoreTake(driver->mutex, portMAX_DELAY);
        uint8_t effect = driver->effect_id;
        xSemaphoreGive(driver->mutex);
        return effect;
    }
    return driver->effect_id;
}

static void app_driver_button_toggle_cb(void *button_handle, void *usr_data)
{
    ESP_LOGI(TAG, "Toggle button pressed");

    // Schedule attribute access on the Matter thread to ensure thread safety
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        // Get current on/off state and toggle it
        attribute_t *attribute = attribute::get(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
        if (attribute == NULL) {
            ESP_LOGE(TAG, "Failed to get OnOff attribute");
            return;
        }

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        attribute::get_val(attribute, &val);
        val.val.b = !val.val.b;
        attribute::update(light_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    });
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                       uint16_t endpoint_id,
                                       uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    light_driver_t *driver = (light_driver_t *)driver_handle;

    if (driver == NULL || driver->mutex == NULL) {
        // Driver not initialized yet - ignore during boot
        return ESP_OK;
    }

    if (endpoint_id != light_endpoint_id) {
        return ESP_OK;  // Not our endpoint
    }

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            err = app_driver_light_set_power(driver_handle, val->val.b);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            err = app_driver_light_set_brightness(driver_handle, val->val.u8);
        }
    } else if (cluster_id == ColorControl::Id) {
        // Protect state modifications with mutex
        xSemaphoreTake(driver->mutex, portMAX_DELAY);
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            driver->hue = val->val.u8;
            driver->color_mode = COLOR_MODE_HSV;
            // Mark HSV update as pending with debounce (issue 7)
            driver->hsv_update_pending = true;
            driver->hsv_update_time = xTaskGetTickCount();
            ESP_LOGD(TAG, "HSV update pending: hue=%d", driver->hue);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            driver->saturation = val->val.u8;
            driver->color_mode = COLOR_MODE_HSV;
            // Mark HSV update as pending with debounce (issue 7)
            driver->hsv_update_pending = true;
            driver->hsv_update_time = xTaskGetTickCount();
            ESP_LOGD(TAG, "HSV update pending: sat=%d", driver->saturation);
        }
        xSemaphoreGive(driver->mutex);

        // Color temperature is handled outside the HSV debounce path: convert
        // mireds -> HSV white point and drive a transition immediately. HA sends
        // these via MoveToColorTemperature when the white slider is used.
        if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            app_driver_light_set_color_temperature_with_transition(
                driver_handle, val->val.u16, TLED_DEFAULT_TRANSITION_MS);
        }

        // Schedule NVS save (debounced)
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id ||
            attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            schedule_save_state_to_nvs();
            // Note: Transition is started by transition_task after debounce period
        }
    }

    return err;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    void *priv_data = endpoint::get_priv_data(endpoint_id);
    if (priv_data == NULL) {
        ESP_LOGE(TAG, "Failed to get priv_data for endpoint %d", endpoint_id);
        return ESP_ERR_INVALID_STATE;
    }
    light_driver_t *driver = (light_driver_t *)priv_data;

    // Get power-on behavior from config
    const tled_config_t *config = tled_config_get();
    uint8_t power_on_behavior = config->power_on_behavior;

    // Try to load saved state from NVS first
    if (load_state_from_nvs() == ESP_OK) {
        // Apply power-on behavior override
        bool original_power = driver->power;
        switch (power_on_behavior) {
            case POWER_ON_RESTORE:
                // Keep the restored power state
                break;
            case POWER_ON_ON:
                driver->power = true;
                ESP_LOGI(TAG, "Power-on behavior: forcing ON");
                break;
            case POWER_ON_OFF:
                driver->power = false;
                ESP_LOGI(TAG, "Power-on behavior: forcing OFF");
                break;
        }

        // Successfully loaded saved state - update Matter attributes to match
        esp_matter_attr_val_t val;

        val = esp_matter_bool(driver->power);
        attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);

        val = esp_matter_uint8(driver->brightness);
        attribute::update(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val);

        val = esp_matter_uint8(driver->hue);
        attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, &val);

        val = esp_matter_uint8(driver->saturation);
        attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id, &val);

        // Restore color mode (HSV or CT). load_state_from_nvs() already set
        // driver->color_mode; reflect CT to Matter so Home Assistant shows the
        // right active mode and temperature. Anything else falls back to HSV.
        if (driver->color_mode == COLOR_MODE_CT) {
            val = esp_matter_uint16(driver->color_temp_mireds);
            attribute::update(endpoint_id, ColorControl::Id,
                              ColorControl::Attributes::ColorTemperatureMireds::Id, &val);
            uint8_t ct_mode = static_cast<uint8_t>(ColorControl::ColorModeEnum::kColorTemperatureMireds);
            val = esp_matter_enum8(ct_mode);
            attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id, &val);
            attribute::update(endpoint_id, ColorControl::Id, ColorControl::Attributes::EnhancedColorMode::Id, &val);
        } else {
            driver->color_mode = COLOR_MODE_HSV;
        }

        ESP_LOGI(TAG, "Restored from NVS: power=%d (was %d), brightness=%d, mode=%d, hue=%d, sat=%d, ct=%umireds",
                 driver->power, original_power, driver->brightness, driver->color_mode,
                 driver->hue, driver->saturation, driver->color_temp_mireds);

        // Initialize transition state
        driver->transition.current_hue = driver->hue;
        driver->transition.current_sat = driver->saturation;
        driver->transition.current_val = driver->power ? driver->brightness : 0;
        driver->transition.start_hue = driver->transition.current_hue;
        driver->transition.start_sat = driver->transition.current_sat;
        driver->transition.start_val = driver->transition.current_val;

        return update_immediate(driver);
    }

    // No saved state - read from Matter attributes (first boot)
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    // Get and apply brightness
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (attribute) {
        attribute::get_val(attribute, &val);
        driver->brightness = val.val.u8;
    }

    // Get and apply hue
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
    if (attribute) {
        attribute::get_val(attribute, &val);
        driver->hue = val.val.u8;
    }

    // Get and apply saturation
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
    if (attribute) {
        attribute::get_val(attribute, &val);
        driver->saturation = val.val.u8;
    }

    driver->color_mode = COLOR_MODE_HSV;

    // Get and apply power state
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    if (attribute) {
        attribute::get_val(attribute, &val);
        driver->power = val.val.b;
    }

    ESP_LOGI(TAG, "First boot defaults: power=%d, brightness=%d, hue=%d, sat=%d",
             driver->power, driver->brightness, driver->hue, driver->saturation);

    // Initialize transition state
    driver->transition.current_hue = driver->hue;
    driver->transition.current_sat = driver->saturation;
    driver->transition.current_val = driver->power ? driver->brightness : 0;
    driver->transition.start_hue = driver->transition.current_hue;
    driver->transition.start_sat = driver->transition.current_sat;
    driver->transition.start_val = driver->transition.current_val;

    return update_immediate(driver);
}

app_driver_handle_t app_driver_light_init(void)
{
    // Load config values
    const tled_config_t *config = tled_config_get();
    s_light_driver.num_leds = config->num_leds;
    s_light_driver.gpio_pin = config->gpio_pin;
    s_light_driver.max_brightness = config->max_brightness;
    s_light_driver.rgb_order = config->rgb_order;
    s_light_driver.white_mode = config->white_mode;
    s_light_driver.manual_white = config->manual_white;
    s_light_driver.gain_r = config->gain_r;
    s_light_driver.gain_g = config->gain_g;
    s_light_driver.gain_b = config->gain_b;
    s_light_driver.gain_w = config->gain_w;
    s_light_driver.white_led_kelvin = config->white_led_kelvin;

    static const char *rgb_order_names[] = {"GRB", "RGB", "BRG", "RBG", "BGR", "GBR"};
    static const char *white_mode_names[] = {"accurate", "brighter", "none", "dual", "max"};
    const char *order_name = (s_light_driver.rgb_order <= RGB_ORDER_GBR)
                             ? rgb_order_names[s_light_driver.rgb_order] : "???";
    const char *white_mode_name = (s_light_driver.white_mode <= WHITE_MODE_MAX)
                                  ? white_mode_names[s_light_driver.white_mode] : "???";
    ESP_LOGI(TAG, "Initializing LED strip driver on GPIO%d with %d LEDs (max brightness %d, order %s, white mode %s, gains %d/%d/%d/%d)",
             s_light_driver.gpio_pin, s_light_driver.num_leds, s_light_driver.max_brightness,
             order_name, white_mode_name, s_light_driver.gain_r, s_light_driver.gain_g,
             s_light_driver.gain_b, s_light_driver.gain_w);

    // Create mutex for thread-safe access
    s_light_driver.mutex = xSemaphoreCreateMutex();
    if (s_light_driver.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return NULL;
    }

    // Determine if RGBW mode based on chipset
    s_light_driver.is_rgbw = (config->chipset == CHIPSET_SK6812);

    // Buffer size: use max of configured LEDs or 100 to clear leftover data
    uint16_t strip_buffer_size = s_light_driver.num_leds > 100 ? s_light_driver.num_leds : 100;

    // Determine LED model
    led_model_t led_model;
    switch (config->chipset) {
        case CHIPSET_SK6812: led_model = LED_MODEL_SK6812; break;
        case CHIPSET_WS2811: led_model = LED_MODEL_WS2812; break;
        default:             led_model = LED_MODEL_WS2812; break;
    }

    // Configure LED strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = s_light_driver.gpio_pin,
        .max_leds = strip_buffer_size,
        .led_pixel_format = s_light_driver.is_rgbw ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB,
        .led_model = led_model,
        .flags = { .invert_out = false },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_light_driver.strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGI(TAG, "LED strip created: model=%s, format=%s",
             s_light_driver.is_rgbw ? "SK6812" : "WS2812",
             s_light_driver.is_rgbw ? "GRBW" : "GRB");

    // Clear all LEDs in buffer (turns off any leftover LEDs from previous config)
    led_strip_clear(s_light_driver.strip);

    // Create NVS save timer for debounced writes (issue 8)
    s_light_driver.nvs_save_timer = xTimerCreate(
        "nvs_save",
        pdMS_TO_TICKS(NVS_SAVE_DEBOUNCE_MS),
        pdFALSE,  // One-shot timer
        NULL,
        nvs_save_timer_callback
    );
    if (s_light_driver.nvs_save_timer == NULL) {
        ESP_LOGW(TAG, "Failed to create NVS save timer, saves will be immediate");
    } else {
        ESP_LOGI(TAG, "NVS save timer created (debounce: %dms)", NVS_SAVE_DEBOUNCE_MS);
    }

    // Start the transition/effect task
    BaseType_t ret = xTaskCreate(
        transition_task,
        "tled_transition",
        TLED_TRANSITION_TASK_STACK,
        &s_light_driver,
        TLED_TRANSITION_TASK_PRIO,
        &s_light_driver.task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create transition task");
        return NULL;
    }

    ESP_LOGI(TAG, "LED strip initialized successfully with transition engine");

    return (app_driver_handle_t)&s_light_driver;
}

app_driver_handle_t app_driver_button_init(void)
{
    ESP_LOGI(TAG, "Initializing button driver on GPIO%d", TLED_BOOT_BUTTON_GPIO);

    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = TLED_BOOT_BUTTON_GPIO,
        .active_level = 0  // Active low (pressed = 0)
    };

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device");
        return NULL;
    }

    esp_err_t err = iot_button_register_cb(handle, BUTTON_PRESS_DOWN, NULL, app_driver_button_toggle_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callback: %s", esp_err_to_name(err));
    }

    return (app_driver_handle_t)handle;
}
