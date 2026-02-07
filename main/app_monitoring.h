/*
 * TLED Health Monitoring
 * Watchdog, memory, thermal monitoring for production reliability
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize health monitoring subsystem
 * - Starts periodic health check task
 * - Logs initial memory state
 * - Initializes thermal sensor
 */
esp_err_t monitoring_init(void);

/**
 * Get current free heap in bytes
 */
size_t monitoring_get_free_heap(void);

/**
 * Get minimum free heap since boot (high water mark)
 */
size_t monitoring_get_min_free_heap(void);

/**
 * Get chip temperature in degrees Celsius
 * Returns -999.0 on error
 */
float monitoring_get_temperature(void);

/**
 * Log current health status (heap, temp, uptime)
 */
void monitoring_log_health(void);

#ifdef __cplusplus
}
#endif
