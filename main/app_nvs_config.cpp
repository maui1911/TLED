/*
 * TLED - Matter-over-Thread LED Controller
 * NVS Configuration Management Implementation
 */

#include "app_nvs_config.h"
#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "tled_config";

#define NVS_NAMESPACE "tled_cfg"
#define NVS_KEY_CONFIG "config"
#define TLED_CONFIG_VERSION_POWER_ON 2

// Valid GPIO pins for ESP32-C6 LED data output
// Avoiding: 9 (boot button), 12-13 (USB), 15 (onboard LED)
static const uint8_t valid_gpio_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 14, 18, 19, 20, 21, 22, 23};
#define NUM_VALID_GPIOS (sizeof(valid_gpio_pins) / sizeof(valid_gpio_pins[0]))

// Current configuration (in RAM)
static tled_config_t s_config;
static bool s_initialized = false;
static SemaphoreHandle_t s_config_mutex = NULL;

// Set defaults
static void set_defaults(tled_config_t *config)
{
    config->num_leds = TLED_DEFAULT_NUM_LEDS;
    config->gpio_pin = TLED_DEFAULT_GPIO_PIN;
    config->rgb_order = TLED_DEFAULT_RGB_ORDER;
    config->chipset = TLED_DEFAULT_CHIPSET;
    config->max_brightness = TLED_DEFAULT_MAX_BRIGHTNESS;
    config->power_on_behavior = TLED_DEFAULT_POWER_ON;
    config->white_mode = TLED_DEFAULT_WHITE_MODE;
    config->manual_white = TLED_DEFAULT_MANUAL_WHITE;
    config->gain_r = TLED_DEFAULT_CHANNEL_GAIN;
    config->gain_g = TLED_DEFAULT_CHANNEL_GAIN;
    config->gain_b = TLED_DEFAULT_CHANNEL_GAIN;
    config->gain_w = TLED_DEFAULT_CHANNEL_GAIN;
    config->white_led_kelvin = TLED_DEFAULT_WHITE_LED_KELVIN;
    strncpy(config->device_name, TLED_DEFAULT_DEVICE_NAME, sizeof(config->device_name) - 1);
    config->device_name[sizeof(config->device_name) - 1] = '\0';
    config->config_version = TLED_CONFIG_VERSION;
    config->configured = false;
}

typedef struct {
    uint16_t num_leds;
    uint8_t gpio_pin;
    uint8_t rgb_order;
    uint8_t chipset;
    uint8_t max_brightness;
    uint8_t power_on_behavior;
    char device_name[32];
    uint8_t config_version;
    bool configured;
} tled_config_v2_t;

static void migrate_v2_config(const tled_config_v2_t *old_config, tled_config_t *new_config)
{
    set_defaults(new_config);
    new_config->num_leds = old_config->num_leds;
    new_config->gpio_pin = old_config->gpio_pin;
    new_config->rgb_order = old_config->rgb_order;
    new_config->chipset = old_config->chipset;
    new_config->max_brightness = old_config->max_brightness;
    new_config->power_on_behavior = old_config->power_on_behavior;
    strncpy(new_config->device_name, old_config->device_name, sizeof(new_config->device_name) - 1);
    new_config->device_name[sizeof(new_config->device_name) - 1] = '\0';
    new_config->configured = old_config->configured;
    new_config->config_version = TLED_CONFIG_VERSION;
}

// v3 layout: tled_config_t before the white_led_kelvin field was added.
#define TLED_CONFIG_VERSION_RGBW 3
typedef struct {
    uint16_t num_leds;
    uint8_t gpio_pin;
    uint8_t rgb_order;
    uint8_t chipset;
    uint8_t max_brightness;
    uint8_t power_on_behavior;
    uint8_t white_mode;
    uint8_t manual_white;
    uint8_t gain_r;
    uint8_t gain_g;
    uint8_t gain_b;
    uint8_t gain_w;
    char device_name[32];
    uint8_t config_version;
    bool configured;
} tled_config_v3_t;

static void migrate_v3_config(const tled_config_v3_t *old_config, tled_config_t *new_config)
{
    set_defaults(new_config);
    new_config->num_leds = old_config->num_leds;
    new_config->gpio_pin = old_config->gpio_pin;
    new_config->rgb_order = old_config->rgb_order;
    new_config->chipset = old_config->chipset;
    new_config->max_brightness = old_config->max_brightness;
    new_config->power_on_behavior = old_config->power_on_behavior;
    new_config->white_mode = old_config->white_mode;
    new_config->manual_white = old_config->manual_white;
    new_config->gain_r = old_config->gain_r;
    new_config->gain_g = old_config->gain_g;
    new_config->gain_b = old_config->gain_b;
    new_config->gain_w = old_config->gain_w;
    // white_led_kelvin stays at its default (set_defaults)
    strncpy(new_config->device_name, old_config->device_name, sizeof(new_config->device_name) - 1);
    new_config->device_name[sizeof(new_config->device_name) - 1] = '\0';
    new_config->configured = old_config->configured;
    new_config->config_version = TLED_CONFIG_VERSION;
}

// Validate configuration
static bool validate_config(const tled_config_t *config)
{
    // Check LED count
    if (config->num_leds == 0 || config->num_leds > 1000) {
        ESP_LOGW(TAG, "Invalid LED count: %d", config->num_leds);
        return false;
    }

    // Check GPIO
    if (!tled_config_validate_gpio(config->gpio_pin)) {
        ESP_LOGW(TAG, "Invalid GPIO pin: %d", config->gpio_pin);
        return false;
    }

    // Check RGB order
    if (config->rgb_order > RGB_ORDER_GBR) {
        ESP_LOGW(TAG, "Invalid RGB order: %d", config->rgb_order);
        return false;
    }

    // Check chipset
    if (config->chipset > CHIPSET_SK6812) {
        ESP_LOGW(TAG, "Invalid chipset: %d", config->chipset);
        return false;
    }

    // Check power-on behavior
    if (config->power_on_behavior > POWER_ON_OFF) {
        ESP_LOGW(TAG, "Invalid power-on behavior: %d", config->power_on_behavior);
        return false;
    }

    // Check RGBW white mode
    if (config->white_mode > WHITE_MODE_MAX) {
        ESP_LOGW(TAG, "Invalid white mode: %d", config->white_mode);
        return false;
    }

    // Check white LED color temperature (plausible LED range)
    if (config->white_led_kelvin < 1000 || config->white_led_kelvin > 20000) {
        ESP_LOGW(TAG, "Invalid white LED kelvin: %d", config->white_led_kelvin);
        return false;
    }

    // Check config version
    if (config->config_version != TLED_CONFIG_VERSION) {
        ESP_LOGW(TAG, "Config version mismatch: %d (expected %d)",
                 config->config_version, TLED_CONFIG_VERSION);
        return false;
    }

    return true;
}

esp_err_t tled_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Create mutex for thread-safe config access
    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        if (s_config_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create config mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Start with defaults
    set_defaults(&s_config);

    // Try to load from NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_OK) {
        size_t size = 0;
        err = nvs_get_blob(handle, NVS_KEY_CONFIG, NULL, &size);

        if (err == ESP_OK && size == sizeof(tled_config_t)) {
            err = nvs_get_blob(handle, NVS_KEY_CONFIG, &s_config, &size);
            // Validate loaded config
            if (err == ESP_OK && validate_config(&s_config)) {
                nvs_close(handle);
                ESP_LOGI(TAG, "Config loaded: %d LEDs, GPIO%d, order=%d, chipset=%d, max_bri=%d, white_mode=%d, gains=%d/%d/%d/%d, name=%s",
                         s_config.num_leds, s_config.gpio_pin, s_config.rgb_order,
                         s_config.chipset, s_config.max_brightness, s_config.white_mode,
                         s_config.gain_r, s_config.gain_g, s_config.gain_b, s_config.gain_w,
                         s_config.device_name);
                s_initialized = true;
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "Loaded config invalid, using defaults");
                set_defaults(&s_config);
            }
        } else if (err == ESP_OK && size == sizeof(tled_config_v2_t)) {
            tled_config_v2_t old_config;
            err = nvs_get_blob(handle, NVS_KEY_CONFIG, &old_config, &size);
            if (err == ESP_OK && old_config.config_version == TLED_CONFIG_VERSION_POWER_ON) {
                migrate_v2_config(&old_config, &s_config);
                if (validate_config(&s_config)) {
                    nvs_close(handle);
                    ESP_LOGI(TAG, "Migrated v2 config: %d LEDs, GPIO%d, order=%d, chipset=%d, max_bri=%d, name=%s",
                             s_config.num_leds, s_config.gpio_pin, s_config.rgb_order,
                             s_config.chipset, s_config.max_brightness, s_config.device_name);
                    s_initialized = true;
                    return ESP_OK;
                }
            }
            ESP_LOGW(TAG, "Failed to migrate config blob, using defaults");
            set_defaults(&s_config);
        } else if (err == ESP_OK && size == sizeof(tled_config_v3_t)) {
            tled_config_v3_t old_config;
            err = nvs_get_blob(handle, NVS_KEY_CONFIG, &old_config, &size);
            if (err == ESP_OK && old_config.config_version == TLED_CONFIG_VERSION_RGBW) {
                migrate_v3_config(&old_config, &s_config);
                if (validate_config(&s_config)) {
                    nvs_close(handle);
                    ESP_LOGI(TAG, "Migrated v3 config: %d LEDs, GPIO%d, white_mode=%d, white_led=%dK",
                             s_config.num_leds, s_config.gpio_pin, s_config.white_mode,
                             s_config.white_led_kelvin);
                    s_initialized = true;
                    return ESP_OK;
                }
            }
            ESP_LOGW(TAG, "Failed to migrate v3 config blob, using defaults");
            set_defaults(&s_config);
        } else {
            ESP_LOGW(TAG, "Failed to load config blob, using defaults");
            set_defaults(&s_config);
        }
        nvs_close(handle);
    } else {
        ESP_LOGI(TAG, "No config in NVS (first boot), using defaults");
    }

    s_initialized = true;
    return ESP_OK;
}

const tled_config_t* tled_config_get(void)
{
    if (!s_initialized) {
        tled_config_init();
    }
    return &s_config;
}

tled_config_t* tled_config_get_mutable(void)
{
    if (!s_initialized) {
        tled_config_init();
    }
    return &s_config;
}

bool tled_config_is_configured(void)
{
    if (!s_initialized) {
        tled_config_init();
    }
    return s_config.configured;
}

esp_err_t tled_config_set(uint16_t num_leds, uint8_t gpio_pin, uint8_t rgb_order,
                          uint8_t chipset, uint8_t max_brightness, const char* device_name)
{
    if (!s_initialized) {
        tled_config_init();
    }

    // Validate inputs (before taking mutex)
    if (num_leds == 0 || num_leds > 1000) {
        ESP_LOGE(TAG, "Invalid LED count: %d (must be 1-1000)", num_leds);
        return ESP_ERR_INVALID_ARG;
    }

    if (!tled_config_validate_gpio(gpio_pin)) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", gpio_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (rgb_order > RGB_ORDER_GBR) {
        ESP_LOGE(TAG, "Invalid RGB order: %d", rgb_order);
        return ESP_ERR_INVALID_ARG;
    }

    if (chipset > CHIPSET_SK6812) {
        ESP_LOGE(TAG, "Invalid chipset: %d", chipset);
        return ESP_ERR_INVALID_ARG;
    }

    // Apply settings with mutex protection
    if (s_config_mutex) {
        xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    }

    s_config.num_leds = num_leds;
    s_config.gpio_pin = gpio_pin;
    s_config.rgb_order = rgb_order;
    s_config.chipset = chipset;
    s_config.max_brightness = max_brightness;

    if (device_name != NULL && strlen(device_name) > 0) {
        strncpy(s_config.device_name, device_name, sizeof(s_config.device_name) - 1);
        s_config.device_name[sizeof(s_config.device_name) - 1] = '\0';
    }

    s_config.configured = true;

    if (s_config_mutex) {
        xSemaphoreGive(s_config_mutex);
    }

    ESP_LOGI(TAG, "Config set: %d LEDs, GPIO%d, order=%d, chipset=%d, max_bri=%d, name=%s",
             s_config.num_leds, s_config.gpio_pin, s_config.rgb_order,
             s_config.chipset, s_config.max_brightness, s_config.device_name);

    return ESP_OK;
}

esp_err_t tled_config_save(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_CONFIG, &s_config, sizeof(tled_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config blob: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    return err;
}

void tled_config_reset_to_defaults(void)
{
    if (s_config_mutex) {
        xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    }

    set_defaults(&s_config);

    if (s_config_mutex) {
        xSemaphoreGive(s_config_mutex);
    }

    ESP_LOGI(TAG, "Config reset to defaults");
}

bool tled_config_validate_gpio(uint8_t gpio_pin)
{
    for (size_t i = 0; i < NUM_VALID_GPIOS; i++) {
        if (valid_gpio_pins[i] == gpio_pin) {
            return true;
        }
    }
    return false;
}
