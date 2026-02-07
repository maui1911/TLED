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
    strncpy(config->device_name, TLED_DEFAULT_DEVICE_NAME, sizeof(config->device_name) - 1);
    config->device_name[sizeof(config->device_name) - 1] = '\0';
    config->config_version = TLED_CONFIG_VERSION;
    config->configured = false;
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
        size_t size = sizeof(tled_config_t);
        err = nvs_get_blob(handle, NVS_KEY_CONFIG, &s_config, &size);
        nvs_close(handle);

        if (err == ESP_OK && size == sizeof(tled_config_t)) {
            // Validate loaded config
            if (validate_config(&s_config)) {
                ESP_LOGI(TAG, "Config loaded: %d LEDs, GPIO%d, order=%d, chipset=%d, max_bri=%d, name=%s",
                         s_config.num_leds, s_config.gpio_pin, s_config.rgb_order,
                         s_config.chipset, s_config.max_brightness, s_config.device_name);
                s_initialized = true;
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "Loaded config invalid, using defaults");
                set_defaults(&s_config);
            }
        } else {
            ESP_LOGW(TAG, "Failed to load config blob, using defaults");
            set_defaults(&s_config);
        }
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
