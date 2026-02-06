/*
 * TLED - Matter-over-Thread LED Controller
 * NVS Configuration Management
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>


// RGB byte order options
typedef enum {
    RGB_ORDER_GRB = 0,
    RGB_ORDER_RGB = 1,
    RGB_ORDER_BRG = 2,
    RGB_ORDER_RBG = 3,
    RGB_ORDER_BGR = 4,
    RGB_ORDER_GBR = 5,
} tled_rgb_order_t;

// LED chipset types
typedef enum {
    CHIPSET_WS2812B = 0,
    CHIPSET_WS2811 = 1,
    CHIPSET_SK6812 = 2,
} tled_chipset_t;

// Configuration structure
typedef struct {
    uint16_t num_leds;          // Number of LEDs (1-1000)
    uint8_t gpio_pin;           // Data GPIO pin
    uint8_t rgb_order;          // RGB byte order (tled_rgb_order_t)
    uint8_t chipset;            // LED chipset (tled_chipset_t)
    uint8_t max_brightness;     // Max brightness limit (0-255)
    char device_name[32];       // Custom device name
    uint8_t config_version;     // Config version for migration
    bool configured;            // True if config has been set
} tled_config_t;

// Default configuration values from Kconfig (menuconfig)
#include <sdkconfig.h>

#define TLED_DEFAULT_NUM_LEDS       CONFIG_TLED_NUM_LEDS
#define TLED_DEFAULT_GPIO_PIN       CONFIG_TLED_GPIO_PIN
#define TLED_DEFAULT_MAX_BRIGHTNESS CONFIG_TLED_MAX_BRIGHTNESS

// RGB order from Kconfig choice
#if defined(CONFIG_TLED_RGB_ORDER_GRB)
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_GRB
#elif defined(CONFIG_TLED_RGB_ORDER_RGB)
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_RGB
#elif defined(CONFIG_TLED_RGB_ORDER_BRG)
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_BRG
#elif defined(CONFIG_TLED_RGB_ORDER_RBG)
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_RBG
#elif defined(CONFIG_TLED_RGB_ORDER_BGR)
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_BGR
#elif defined(CONFIG_TLED_RGB_ORDER_GBR)
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_GBR
#else
#define TLED_DEFAULT_RGB_ORDER      RGB_ORDER_GRB
#endif

// LED type from Kconfig choice
#if defined(CONFIG_TLED_LED_WS2812B)
#define TLED_DEFAULT_CHIPSET        CHIPSET_WS2812B
#elif defined(CONFIG_TLED_LED_WS2811)
#define TLED_DEFAULT_CHIPSET        CHIPSET_WS2811
#elif defined(CONFIG_TLED_LED_SK6812)
#define TLED_DEFAULT_CHIPSET        CHIPSET_SK6812
#else
#define TLED_DEFAULT_CHIPSET        CHIPSET_WS2812B
#endif

#define TLED_DEFAULT_DEVICE_NAME    "TLED"
#define TLED_CONFIG_VERSION         1

/**
 * @brief Initialize the config module
 *
 * Loads config from NVS if available, otherwise uses defaults.
 *
 * @return ESP_OK on success
 */
esp_err_t tled_config_init(void);

/**
 * @brief Get pointer to current configuration
 *
 * @return Pointer to config structure (read-only recommended)
 */
const tled_config_t* tled_config_get(void);

/**
 * @brief Check if device has been configured
 *
 * @return true if config exists in NVS, false if first boot
 */
bool tled_config_is_configured(void);

/**
 * @brief Set a configuration value
 *
 * Does not save to NVS until tled_config_save() is called.
 *
 * @param num_leds Number of LEDs
 * @param gpio_pin GPIO pin for data
 * @param rgb_order RGB byte order
 * @param chipset LED chipset type
 * @param max_brightness Maximum brightness limit
 * @param device_name Device name (max 31 chars)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if validation fails
 */
esp_err_t tled_config_set(uint16_t num_leds, uint8_t gpio_pin, uint8_t rgb_order,
                          uint8_t chipset, uint8_t max_brightness, const char* device_name);

/**
 * @brief Save current configuration to NVS
 *
 * @return ESP_OK on success
 */
esp_err_t tled_config_save(void);

/**
 * @brief Reset configuration to defaults
 *
 * Does not save to NVS until tled_config_save() is called.
 */
void tled_config_reset_to_defaults(void);

/**
 * @brief Validate a GPIO pin for LED data output
 *
 * @param gpio_pin GPIO pin number
 * @return true if pin is valid for LED data
 */
bool tled_config_validate_gpio(uint8_t gpio_pin);

