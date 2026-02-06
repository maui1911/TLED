/*
 * TLED - Matter-over-Thread LED Controller
 * Configuration constants
 */

#pragma once

#include <sdkconfig.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

// Hardware Configuration - DFRobot Beetle ESP32-C6
#define TLED_ONBOARD_LED_GPIO   15      // Onboard LED (fallback)
#define TLED_BOOT_BUTTON_GPIO   9       // Boot button

// Color remapping constants (Matter uses 0-254 range)
#define MATTER_BRIGHTNESS_MAX   254
#define MATTER_HUE_MAX          254
#define MATTER_SATURATION_MAX   254
#define STANDARD_HUE_MAX        360
#define STANDARD_SATURATION_MAX 100
#define STANDARD_BRIGHTNESS_MAX 100

// Matter default values
#define TLED_DEFAULT_POWER      false
#define TLED_DEFAULT_BRIGHTNESS 127     // 50% brightness (0-254)

// Transition settings
#define TLED_TRANSITION_TASK_STACK  4096
#define TLED_TRANSITION_TASK_PRIO   5
#define TLED_TRANSITION_TICK_MS     20      // 50 FPS update rate
#define TLED_DEFAULT_TRANSITION_MS  CONFIG_TLED_DEFAULT_TRANSITION_MS

// Effect IDs
#define TLED_EFFECT_NONE        0
#define TLED_EFFECT_RAINBOW     1
#define TLED_EFFECT_BREATHING   2
#define TLED_EFFECT_CANDLE      3
#define TLED_EFFECT_CHASE       4

// Device identification
#define TLED_DEVICE_NAME        "TLED"
#define TLED_VENDOR_NAME        "TLED Project"

// OpenThread configuration for ESP32-C6 native radio
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
