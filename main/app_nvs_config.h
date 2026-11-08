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
    CHIPSET_WS2805 = 3,     // 5-channel RGB + warm white + cool white
} tled_chipset_t;

// WS2805 white channel order: which wire channel (W1/W2) is warm vs cool.
// Strip vendors wire W1/W2 to 3000K/6500K inconsistently.
typedef enum {
    WHITE_ORDER_WW_CW = 0,  // W1 = warm white, W2 = cool white (most strips)
    WHITE_ORDER_CW_WW = 1,  // W1 = cool white, W2 = warm white
} tled_white_order_t;

// Sentinel for "BIN (backup data) pin not used"
#define TLED_BIN_GPIO_DISABLED  0xFF

// Power-on behavior options
typedef enum {
    POWER_ON_RESTORE = 0,   // Restore last state (default)
    POWER_ON_ON = 1,        // Always turn on
    POWER_ON_OFF = 2,       // Always stay off
} tled_power_on_t;

// SK6812 RGBW white-channel mixing mode
typedef enum {
    WHITE_MODE_ACCURATE = 0, // Extract common RGB into W, preserving color accuracy
    WHITE_MODE_BRIGHTER = 1, // Add common RGB to W while keeping RGB channels
    WHITE_MODE_NONE = 2,     // No automatic white extraction, manual W only
    WHITE_MODE_DUAL = 3,     // Manual W when set, otherwise brighter mode
    WHITE_MODE_MAX = 4,      // Drive W from the strongest RGB channel
} tled_white_mode_t;

// Configuration structure
typedef struct {
    uint16_t num_leds;          // Number of LEDs (1-1000)
    uint8_t gpio_pin;           // Data GPIO pin
    uint8_t rgb_order;          // RGB byte order (tled_rgb_order_t)
    uint8_t chipset;            // LED chipset (tled_chipset_t)
    uint8_t max_brightness;     // Max brightness limit (0-255)
    uint8_t power_on_behavior;  // Power-on behavior (tled_power_on_t)
    uint8_t white_mode;         // RGBW white mixing mode (tled_white_mode_t)
    uint8_t manual_white;       // Manual white channel level (0-255)
    uint8_t gain_r;             // Red channel gain (0-255)
    uint8_t gain_g;             // Green channel gain (0-255)
    uint8_t gain_b;             // Blue channel gain (0-255)
    uint8_t gain_w;             // White channel gain (0-255)
    uint8_t bin_gpio;           // WS2805 backup data GPIO (TLED_BIN_GPIO_DISABLED = off)
    uint8_t white_order;        // WS2805 white channel order (tled_white_order_t)
    uint16_t white_led_kelvin;  // Native color temp of the SK6812 W LED (K)
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
#elif defined(CONFIG_TLED_LED_WS2805)
#define TLED_DEFAULT_CHIPSET        CHIPSET_WS2805
#else
#define TLED_DEFAULT_CHIPSET        CHIPSET_WS2812B
#endif

// WS2805 BIN (backup data) pin from Kconfig (-1 = disabled)
#if defined(CONFIG_TLED_WS2805_BIN_GPIO) && (CONFIG_TLED_WS2805_BIN_GPIO >= 0)
#define TLED_DEFAULT_BIN_GPIO       CONFIG_TLED_WS2805_BIN_GPIO
#else
#define TLED_DEFAULT_BIN_GPIO       TLED_BIN_GPIO_DISABLED
#endif

#define TLED_DEFAULT_DEVICE_NAME    "TLED"
#define TLED_DEFAULT_POWER_ON       POWER_ON_RESTORE
#define TLED_DEFAULT_WHITE_MODE     WHITE_MODE_ACCURATE
#define TLED_DEFAULT_MANUAL_WHITE   0
#define TLED_DEFAULT_CHANNEL_GAIN   255
#define TLED_DEFAULT_WHITE_ORDER    WHITE_ORDER_WW_CW
#define TLED_DEFAULT_WHITE_LED_KELVIN 4000  // Neutral-white SK6812 default
#define TLED_CONFIG_VERSION         5  // Bumped for white LED color temperature

/**
 * @brief Initialize the config module
 *
 * Loads config from NVS if available, otherwise uses defaults.
 *
 * @return ESP_OK on success
 */
esp_err_t tled_config_init(void);

/**
 * @brief Get pointer to current configuration (read-only)
 *
 * @return Pointer to config structure
 */
const tled_config_t* tled_config_get(void);

/**
 * @brief Get mutable pointer to current configuration
 *
 * Use for modifying config values. Call tled_config_save() to persist.
 *
 * @return Mutable pointer to config structure
 */
tled_config_t* tled_config_get_mutable(void);

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

// Alias for reset function
#define tled_config_reset() tled_config_reset_to_defaults()
