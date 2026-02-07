/*
 * TLED Health Monitoring Implementation
 */

#include "app_monitoring.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "tled_monitor";

// Health check interval (60 seconds)
#define HEALTH_CHECK_INTERVAL_MS 60000

// Temperature warning threshold (Celsius)
#define TEMP_WARNING_THRESHOLD 70.0f
#define TEMP_CRITICAL_THRESHOLD 85.0f

// Memory warning threshold (bytes)
#define HEAP_WARNING_THRESHOLD 20000

static temperature_sensor_handle_t s_temp_sensor = NULL;
static bool s_temp_sensor_enabled = false;
static size_t s_initial_free_heap = 0;

// Forward declaration
static void health_check_task(void *pvParameters);

size_t monitoring_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

size_t monitoring_get_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

float monitoring_get_temperature(void)
{
    if (!s_temp_sensor_enabled || !s_temp_sensor) {
        return -999.0f;
    }

    float temp = 0;
    esp_err_t ret = temperature_sensor_get_celsius(s_temp_sensor, &temp);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
        return -999.0f;
    }

    return temp;
}

void monitoring_log_health(void)
{
    size_t free_heap = monitoring_get_free_heap();
    size_t min_heap = monitoring_get_min_free_heap();
    float temp = monitoring_get_temperature();
    int64_t uptime_us = esp_timer_get_time();
    int uptime_sec = (int)(uptime_us / 1000000);
    int hours = uptime_sec / 3600;
    int mins = (uptime_sec % 3600) / 60;
    int secs = uptime_sec % 60;

    // Calculate heap usage
    size_t heap_used = s_initial_free_heap - free_heap;

    ESP_LOGI(TAG, "Health: heap=%zu/%zu (min=%zu, used=%zu), temp=%.1fC, uptime=%02d:%02d:%02d",
             free_heap, s_initial_free_heap, min_heap, heap_used, temp, hours, mins, secs);

    // Warnings
    if (free_heap < HEAP_WARNING_THRESHOLD) {
        ESP_LOGW(TAG, "LOW MEMORY WARNING: Only %zu bytes free!", free_heap);
    }

    if (temp > TEMP_CRITICAL_THRESHOLD) {
        ESP_LOGE(TAG, "CRITICAL TEMPERATURE: %.1fC - consider reducing brightness!", temp);
    } else if (temp > TEMP_WARNING_THRESHOLD) {
        ESP_LOGW(TAG, "High temperature warning: %.1fC", temp);
    }
}

static void health_check_task(void *pvParameters)
{
    // Wait a bit before first health check
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true) {
        monitoring_log_health();
        vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL_MS));
    }
}

esp_err_t monitoring_init(void)
{
    ESP_LOGI(TAG, "Initializing health monitoring...");

    // Record initial heap state
    s_initial_free_heap = esp_get_free_heap_size();

    ESP_LOGI(TAG, "Initial heap: %zu bytes free", s_initial_free_heap);
    ESP_LOGI(TAG, "Largest free block: %zu bytes",
             heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    // Initialize temperature sensor
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t ret = temperature_sensor_install(&temp_config, &s_temp_sensor);
    if (ret == ESP_OK) {
        ret = temperature_sensor_enable(s_temp_sensor);
        if (ret == ESP_OK) {
            s_temp_sensor_enabled = true;
            float initial_temp = monitoring_get_temperature();
            ESP_LOGI(TAG, "Temperature sensor enabled, current: %.1fC", initial_temp);
        } else {
            ESP_LOGW(TAG, "Failed to enable temp sensor: %s", esp_err_to_name(ret));
            // Clean up on enable failure to prevent resource leak
            temperature_sensor_uninstall(s_temp_sensor);
            s_temp_sensor = NULL;
        }
    } else {
        ESP_LOGW(TAG, "Failed to install temp sensor: %s", esp_err_to_name(ret));
    }

    // Start health monitoring task
    BaseType_t task_ret = xTaskCreate(
        health_check_task,
        "health_monitor",
        2048,
        NULL,
        1,  // Low priority
        NULL
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create health monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Health monitoring initialized (interval: %ds, watchdog: %ds)",
             HEALTH_CHECK_INTERVAL_MS / 1000, CONFIG_ESP_TASK_WDT_TIMEOUT_S);

    return ESP_OK;
}
