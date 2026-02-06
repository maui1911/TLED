/*
 * TLED - Matter-over-Thread LED Controller
 * BLE Configuration Service
 */

#pragma once

#include <esp_err.h>
#include <stdbool.h>


/**
 * @brief Start BLE configuration mode
 *
 * Starts advertising and waits for a BLE connection to configure the device.
 * This function blocks until configuration is complete or cancelled.
 *
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return ESP_OK if configured successfully, ESP_ERR_TIMEOUT if timed out
 */
esp_err_t tled_ble_config_start(uint32_t timeout_ms);

/**
 * @brief Stop BLE configuration mode
 *
 * Stops advertising and disconnects any connected clients.
 */
void tled_ble_config_stop(void);

/**
 * @brief Check if BLE config mode is active
 *
 * @return true if BLE config mode is running
 */
bool tled_ble_config_is_active(void);

/**
 * @brief Check if configuration was completed via BLE
 *
 * @return true if config was saved during this BLE session
 */
bool tled_ble_config_was_saved(void);

