/*
 * TLED - Matter-over-Thread LED Controller
 * LED Driver Implementation (Phase 3: Transitions & Effects)
 */

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/rmt.h>
#include <led_strip.h>
#include <math.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <esp_matter.h>
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

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "tled_driver";

// External reference to light endpoint ID (defined in app_main.cpp)
extern uint16_t light_endpoint_id;

// Color modes
typedef enum {
    COLOR_MODE_HSV = 0,
    COLOR_MODE_XY = 1,
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

    // Transition timing
    uint32_t transition_start_ms;
    uint32_t transition_duration_ms;
    bool transitioning;
} transition_state_t;

// Driver state structure
typedef struct {
    led_strip_t *strip;
    bool power;
    uint8_t brightness;     // 0-254 (Matter range)
    uint8_t hue;            // 0-254 (Matter range)
    uint8_t saturation;     // 0-254 (Matter range)
    uint16_t color_x;       // 0-65535 (Matter CIE x * 65535)
    uint16_t color_y;       // 0-65535 (Matter CIE y * 65535)
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
} light_driver_t;

// Static driver instance
static light_driver_t s_light_driver = {
    .strip = NULL,
    .power = false,
    .brightness = 127,
    .hue = 0,
    .saturation = 0,
    .color_x = 24939,
    .color_y = 24701,
    .color_mode = COLOR_MODE_HSV,
    .transition = {0},
    .effect_id = TLED_EFFECT_NONE,
    .effect_step = 0,
    .task_handle = NULL,
    .mutex = NULL,
    .num_leds = TLED_DEFAULT_NUM_LEDS,
    .gpio_pin = TLED_DEFAULT_GPIO_PIN,
    .max_brightness = TLED_DEFAULT_MAX_BRIGHTNESS
};

// Forward declarations
static void transition_task(void *arg);
static esp_err_t update_strip_rgb(light_driver_t *driver, uint8_t r, uint8_t g, uint8_t b);

// Save current state to NVS
static esp_err_t save_state_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_u8(handle, NVS_KEY_POWER, s_light_driver.power ? 1 : 0);
    nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, s_light_driver.brightness);
    nvs_set_u8(handle, NVS_KEY_HUE, s_light_driver.hue);
    nvs_set_u8(handle, NVS_KEY_SATURATION, s_light_driver.saturation);

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGD(TAG, "State saved: power=%d, brightness=%d, hue=%d, sat=%d",
                 s_light_driver.power, s_light_driver.brightness,
                 s_light_driver.hue, s_light_driver.saturation);
    }
    return err;
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

// Update strip with specific RGB values
static esp_err_t update_strip_rgb(light_driver_t *driver, uint8_t r, uint8_t g, uint8_t b)
{
    if (driver->strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < driver->num_leds; i++) {
        driver->strip->set_pixel(driver->strip, i, r, g, b);
    }

    return driver->strip->refresh(driver->strip, 100);
}

// Update strip with per-LED RGB array (for chase effects etc)
static esp_err_t update_strip_array(light_driver_t *driver, uint8_t (*colors)[3])
{
    if (driver->strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < driver->num_leds; i++) {
        driver->strip->set_pixel(driver->strip, i, colors[i][0], colors[i][1], colors[i][2]);
    }

    return driver->strip->refresh(driver->strip, 100);
}

// Calculate current interpolated RGB from transition state
static void get_interpolated_rgb(light_driver_t *driver, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // Convert current interpolated values to standard ranges
    uint16_t hue = (uint16_t)((driver->transition.current_hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX);
    uint8_t sat = (uint8_t)((driver->transition.current_sat * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX);
    uint8_t val = (uint8_t)((driver->transition.current_val * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX);

    hsv_to_rgb(hue, sat, val, r, g, b);
}

// Linear interpolation with hue wrap-around handling
static float lerp_hue(float from, float to, float t)
{
    // Handle hue wrap-around (0 and 254 are adjacent on the color wheel)
    float diff = to - from;

    // If the difference is more than half the range, go the short way around
    if (diff > 127.0f) {
        diff -= 254.0f;
    } else if (diff < -127.0f) {
        diff += 254.0f;
    }

    float result = from + diff * t;

    // Wrap result to valid range [0, 254]
    while (result < 0) result += 254.0f;
    while (result > 254.0f) result -= 254.0f;

    return result;
}

static float lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

// Rainbow effect - cycle through hues
static void effect_rainbow(light_driver_t *driver)
{
    // Use effect_step as hue offset
    uint16_t base_hue = driver->effect_step % 360;

    // Get brightness from current setting
    uint8_t val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;

    uint8_t r, g, b;
    hsv_to_rgb(base_hue, 100, val, &r, &g, &b);
    update_strip_rgb(driver, r, g, b);

    driver->effect_step += 2;  // Speed of rainbow
}

// Breathing effect - pulse brightness
static void effect_breathing(light_driver_t *driver)
{
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
    update_strip_rgb(driver, r, g, b);

    driver->effect_step += 3;  // Speed of breathing
}

// Candle flicker effect
static void effect_candle(light_driver_t *driver)
{
    // Random flicker with warm color
    uint8_t max_val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;

    // Random variation: 50-100% of max brightness
    uint8_t variation = (esp_random() % 50) + 50;
    uint8_t val = (max_val * variation) / 100;

    // Warm orange/yellow hue (around 30-40 degrees)
    uint16_t hue = 30 + (esp_random() % 15);

    uint8_t r, g, b;
    hsv_to_rgb(hue, 100, val, &r, &g, &b);
    update_strip_rgb(driver, r, g, b);

    driver->effect_step++;
}

// Chase effect - moving dot
static void effect_chase(light_driver_t *driver)
{
    // Get current color
    uint16_t hue = (driver->hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX;
    uint8_t sat = (driver->saturation * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX;
    uint8_t val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;

    uint8_t r, g, b;
    hsv_to_rgb(hue, sat, val, &r, &g, &b);

    // Current position
    int num_leds = driver->num_leds;
    int pos = driver->effect_step % num_leds;
    int trail = (pos - 1 + num_leds) % num_leds;

    // Set all LEDs
    for (int i = 0; i < num_leds; i++) {
        if (i == pos) {
            driver->strip->set_pixel(driver->strip, i, r, g, b);
        } else if (i == trail) {
            driver->strip->set_pixel(driver->strip, i, r / 3, g / 3, b / 3);
        } else {
            driver->strip->set_pixel(driver->strip, i, 0, 0, 0);
        }
    }

    driver->strip->refresh(driver->strip, 100);
    driver->effect_step++;
}

// Main transition/effect task
static void transition_task(void *arg)
{
    light_driver_t *driver = (light_driver_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        xSemaphoreTake(driver->mutex, portMAX_DELAY);

        if (!driver->power) {
            // Light is off - just set black and wait
            update_strip_rgb(driver, 0, 0, 0);
            xSemaphoreGive(driver->mutex);
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
            continue;
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
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t elapsed = now - driver->transition.transition_start_ms;

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

            // Update strip with interpolated values
            uint8_t r, g, b;
            get_interpolated_rgb(driver, &r, &g, &b);
            update_strip_rgb(driver, r, g, b);
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
    driver->transition.transition_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
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

    uint8_t r, g, b;

    if (!driver->power) {
        r = g = b = 0;
        ESP_LOGI(TAG, "Strip OFF");
    } else {
        uint16_t hue = (driver->hue * STANDARD_HUE_MAX) / MATTER_HUE_MAX;
        uint8_t sat = (driver->saturation * STANDARD_SATURATION_MAX) / MATTER_SATURATION_MAX;
        uint8_t val = (driver->brightness * STANDARD_BRIGHTNESS_MAX) / MATTER_BRIGHTNESS_MAX;
        hsv_to_rgb(hue, sat, val, &r, &g, &b);
        ESP_LOGI(TAG, "HSV mode: H=%d S=%d V=%d -> R=%d G=%d B=%d",
                 driver->hue, driver->saturation, driver->brightness, r, g, b);
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

    driver->power = power;
    ESP_LOGI(TAG, "LED power set to %s", power ? "ON" : "OFF");

    save_state_to_nvs();

    if (power) {
        // Fade in
        start_transition(driver, driver->hue, driver->saturation, driver->brightness,
                        TLED_DEFAULT_TRANSITION_MS);
    } else {
        // Fade out - target brightness 0
        start_transition(driver, driver->hue, driver->saturation, 0,
                        TLED_DEFAULT_TRANSITION_MS);
    }

    return ESP_OK;
}

bool app_driver_light_get_power(app_driver_handle_t handle)
{
    light_driver_t *driver = (light_driver_t *)handle;
    if (driver == NULL) {
        return false;
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

    save_state_to_nvs();

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

    save_state_to_nvs();

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
    ESP_LOGI(TAG, "Color set to H=%d S=%d", hue, saturation);

    save_state_to_nvs();

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
    ESP_LOGI(TAG, "Color set to H=%d S=%d (transition %lums)", hue, saturation, (unsigned long)transition_ms);

    save_state_to_nvs();

    if (transition_ms == 0) {
        return update_immediate(driver);
    }

    start_transition(driver, hue, saturation, driver->brightness, transition_ms);
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
    return driver->effect_id;
}

static void app_driver_button_toggle_cb(void *button_handle, void *usr_data)
{
    ESP_LOGI(TAG, "Toggle button pressed");

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
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                       uint16_t endpoint_id,
                                       uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    light_driver_t *driver = (light_driver_t *)driver_handle;

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
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            driver->hue = val->val.u8;
            driver->color_mode = COLOR_MODE_HSV;
            save_state_to_nvs();
            start_transition(driver, driver->hue, driver->saturation, driver->brightness,
                            TLED_DEFAULT_TRANSITION_MS);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            driver->saturation = val->val.u8;
            driver->color_mode = COLOR_MODE_HSV;
            save_state_to_nvs();
            start_transition(driver, driver->hue, driver->saturation, driver->brightness,
                            TLED_DEFAULT_TRANSITION_MS);
        }
    }

    return err;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    void *priv_data = endpoint::get_priv_data(endpoint_id);
    light_driver_t *driver = (light_driver_t *)priv_data;

    // Try to load saved state from NVS first
    if (load_state_from_nvs() == ESP_OK) {
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

        driver->color_mode = COLOR_MODE_HSV;

        ESP_LOGI(TAG, "Restored from NVS: power=%d, brightness=%d, hue=%d, sat=%d",
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

    ESP_LOGI(TAG, "Initializing LED strip driver on GPIO%d with %d LEDs (max brightness %d)",
             s_light_driver.gpio_pin, s_light_driver.num_leds, s_light_driver.max_brightness);

    // Create mutex for thread-safe access
    s_light_driver.mutex = xSemaphoreCreateMutex();
    if (s_light_driver.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return NULL;
    }

    // Configure RMT for WS2812B
    rmt_config_t rmt_cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)s_light_driver.gpio_pin, RMT_CHANNEL_0);
    rmt_cfg.clk_div = 2;

    esp_err_t err = rmt_config(&rmt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RMT: %s", esp_err_to_name(err));
        return NULL;
    }

    err = rmt_driver_install(rmt_cfg.channel, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install RMT driver: %s", esp_err_to_name(err));
        return NULL;
    }

    // Configure LED strip
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(s_light_driver.num_leds, (led_strip_dev_t)rmt_cfg.channel);
    s_light_driver.strip = led_strip_new_rmt_ws2812(&strip_config);
    if (s_light_driver.strip == NULL) {
        ESP_LOGE(TAG, "Failed to create LED strip");
        rmt_driver_uninstall(rmt_cfg.channel);
        return NULL;
    }

    // Clear strip (all off)
    s_light_driver.strip->clear(s_light_driver.strip, 100);

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
