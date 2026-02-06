/*
 * TLED Serial Configuration
 * Allows runtime configuration via USB serial commands
 */

#pragma once

#include "esp_err.h"

/**
 * Initialize the serial configuration handler
 * Starts a task that listens for commands on UART
 */
esp_err_t serial_config_init(void);

/**
 * Check if currently in config mode
 */
bool serial_config_is_active(void);
