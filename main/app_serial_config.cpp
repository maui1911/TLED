/*
 * TLED Serial Configuration
 * Handles serial commands for runtime configuration
 */

#include "app_serial_config.h"
#include "app_nvs_config.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_system.h"
#include <esp_matter.h>

static const char *TAG = "serial_config";

#define SERIAL_BUF_SIZE 256
#define CMD_BUF_SIZE 128

static bool s_config_active = false;
static char s_cmd_buf[CMD_BUF_SIZE];
static int s_cmd_pos = 0;

// Forward declarations
static void process_command(const char *cmd);
static void print_help(void);
static void print_config(void);
static void handle_set_command(const char *param, const char *value);

static void serial_write(const char *str) {
    usb_serial_jtag_write_bytes((const uint8_t *)str, strlen(str), pdMS_TO_TICKS(100));
}

static void serial_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    serial_write(buf);
}

static void serial_config_task(void *arg) {
    uint8_t rx_buf[64];

    // Print welcome message
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for USB to stabilize
    serial_write("\r\n");
    serial_write("========================================\r\n");
    serial_write("  TLED Serial Configuration\r\n");
    serial_write("  Type 'help' for available commands\r\n");
    serial_write("========================================\r\n");
    print_config();
    serial_write("\r\n> ");

    s_config_active = true;

    while (1) {
        int len = usb_serial_jtag_read_bytes(rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)rx_buf[i];

                // Handle backspace
                if (c == '\b' || c == 127) {
                    if (s_cmd_pos > 0) {
                        s_cmd_pos--;
                        serial_write("\b \b");
                    }
                    continue;
                }

                // Handle enter
                if (c == '\r' || c == '\n') {
                    serial_write("\r\n");
                    s_cmd_buf[s_cmd_pos] = '\0';

                    if (s_cmd_pos > 0) {
                        process_command(s_cmd_buf);
                    }

                    s_cmd_pos = 0;
                    serial_write("> ");
                    continue;
                }

                // Add character to buffer
                if (s_cmd_pos < CMD_BUF_SIZE - 1 && c >= 32 && c < 127) {
                    s_cmd_buf[s_cmd_pos++] = c;
                    // Echo character
                    char echo[2] = {c, '\0'};
                    serial_write(echo);
                }
            }
        }
    }
}

static void process_command(const char *cmd) {
    // Skip leading whitespace
    while (*cmd == ' ') cmd++;

    if (strlen(cmd) == 0) {
        return;
    }

    // Parse command
    char command[32] = {0};
    char param[32] = {0};
    char value[32] = {0};

    int parsed = sscanf(cmd, "%31s %31s %31s", command, param, value);

    if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        print_help();
    }
    else if (strcmp(command, "config") == 0 || strcmp(command, "show") == 0) {
        print_config();
    }
    else if (strcmp(command, "set") == 0 && parsed >= 3) {
        handle_set_command(param, value);
    }
    else if (strcmp(command, "save") == 0) {
        serial_write("Saving configuration to NVS...\r\n");
        esp_err_t err = tled_config_save();
        if (err == ESP_OK) {
            serial_write("Configuration saved successfully!\r\n");
            serial_write("Rebooting in 2 seconds...\r\n");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else {
            serial_printf("Error saving config: %s\r\n", esp_err_to_name(err));
        }
    }
    else if (strcmp(command, "reboot") == 0) {
        serial_write("Rebooting...\r\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    else if (strcmp(command, "factory") == 0) {
        serial_write("Resetting to factory defaults (including Matter fabric)...\r\n");
        tled_config_reset();
        tled_config_save();
        serial_write("Clearing Matter commissioning data...\r\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        // This clears fabric data and reboots
        esp_matter::factory_reset();
    }
    else {
        serial_printf("Unknown command: %s\r\n", command);
        serial_write("Type 'help' for available commands\r\n");
    }
}

static void print_help(void) {
    serial_write("\r\n");
    serial_write("Available commands:\r\n");
    serial_write("  help              - Show this help\r\n");
    serial_write("  config            - Show current configuration\r\n");
    serial_write("  set leds <n>      - Set number of LEDs (1-1000)\r\n");
    serial_write("  set gpio <n>      - Set data GPIO pin (0-21)\r\n");
    serial_write("  set brightness <n> - Set max brightness (1-255)\r\n");
    serial_write("  set type <t>      - Set LED type: ws2812b, ws2811, sk6812\r\n");
    serial_write("  set order <o>     - Set RGB order: grb, rgb, brg, rbg, bgr, gbr\r\n");
    serial_write("  set name <name>   - Set device name\r\n");
    serial_write("  set poweron <m>   - Power-on behavior: restore, on, off\r\n");
    serial_write("  save              - Save config and reboot\r\n");
    serial_write("  reboot            - Reboot without saving\r\n");
    serial_write("  factory           - Reset to factory defaults\r\n");
    serial_write("\r\n");
}

static void print_config(void) {
    const tled_config_t *cfg = tled_config_get();

    const char *type_str = "unknown";
    switch (cfg->chipset) {
        case 0: type_str = "ws2812b"; break;
        case 1: type_str = "ws2811"; break;
        case 2: type_str = "sk6812"; break;
    }

    const char *order_str = "unknown";
    switch (cfg->rgb_order) {
        case RGB_ORDER_GRB: order_str = "grb"; break;
        case RGB_ORDER_RGB: order_str = "rgb"; break;
        case RGB_ORDER_BRG: order_str = "brg"; break;
        case RGB_ORDER_RBG: order_str = "rbg"; break;
        case RGB_ORDER_BGR: order_str = "bgr"; break;
        case RGB_ORDER_GBR: order_str = "gbr"; break;
    }

    const char *poweron_str = "unknown";
    switch (cfg->power_on_behavior) {
        case POWER_ON_RESTORE: poweron_str = "restore"; break;
        case POWER_ON_ON: poweron_str = "on"; break;
        case POWER_ON_OFF: poweron_str = "off"; break;
    }

    serial_write("\r\nCurrent configuration:\r\n");
    serial_printf("  leds       = %d\r\n", cfg->num_leds);
    serial_printf("  gpio       = %d\r\n", cfg->gpio_pin);
    serial_printf("  brightness = %d\r\n", cfg->max_brightness);
    serial_printf("  type       = %s\r\n", type_str);
    serial_printf("  order      = %s\r\n", order_str);
    serial_printf("  poweron    = %s\r\n", poweron_str);
    serial_printf("  name       = %s\r\n", cfg->device_name);
    serial_write("\r\n");
}

static void handle_set_command(const char *param, const char *value) {
    tled_config_t *cfg = tled_config_get_mutable();

    if (strcmp(param, "leds") == 0) {
        int n = atoi(value);
        if (n >= 1 && n <= 1000) {
            cfg->num_leds = n;
            serial_printf("Set leds = %d\r\n", n);
        } else {
            serial_write("Error: leds must be 1-1000\r\n");
        }
    }
    else if (strcmp(param, "gpio") == 0) {
        int n = atoi(value);
        if (tled_config_validate_gpio((uint8_t)n)) {
            cfg->gpio_pin = n;
            serial_printf("Set gpio = %d\r\n", n);
        } else {
            serial_write("Error: invalid GPIO pin (avoid 9, 12-13, 15)\r\n");
        }
    }
    else if (strcmp(param, "brightness") == 0) {
        int n = atoi(value);
        if (n >= 1 && n <= 255) {
            cfg->max_brightness = n;
            serial_printf("Set brightness = %d\r\n", n);
        } else {
            serial_write("Error: brightness must be 1-255\r\n");
        }
    }
    else if (strcmp(param, "type") == 0) {
        if (strcmp(value, "ws2812b") == 0) {
            cfg->chipset = 0;
            serial_write("Set type = ws2812b\r\n");
        } else if (strcmp(value, "ws2811") == 0) {
            cfg->chipset = 1;
            serial_write("Set type = ws2811\r\n");
        } else if (strcmp(value, "sk6812") == 0) {
            cfg->chipset = 2;
            serial_write("Set type = sk6812\r\n");
        } else {
            serial_write("Error: type must be ws2812b, ws2811, or sk6812\r\n");
        }
    }
    else if (strcmp(param, "order") == 0) {
        if (strcmp(value, "grb") == 0) {
            cfg->rgb_order = RGB_ORDER_GRB;
            serial_write("Set order = grb\r\n");
        } else if (strcmp(value, "rgb") == 0) {
            cfg->rgb_order = RGB_ORDER_RGB;
            serial_write("Set order = rgb\r\n");
        } else if (strcmp(value, "brg") == 0) {
            cfg->rgb_order = RGB_ORDER_BRG;
            serial_write("Set order = brg\r\n");
        } else if (strcmp(value, "rbg") == 0) {
            cfg->rgb_order = RGB_ORDER_RBG;
            serial_write("Set order = rbg\r\n");
        } else if (strcmp(value, "bgr") == 0) {
            cfg->rgb_order = RGB_ORDER_BGR;
            serial_write("Set order = bgr\r\n");
        } else if (strcmp(value, "gbr") == 0) {
            cfg->rgb_order = RGB_ORDER_GBR;
            serial_write("Set order = gbr\r\n");
        } else {
            serial_write("Error: order must be grb, rgb, brg, rbg, bgr, or gbr\r\n");
        }
    }
    else if (strcmp(param, "name") == 0) {
        strncpy(cfg->device_name, value, sizeof(cfg->device_name) - 1);
        cfg->device_name[sizeof(cfg->device_name) - 1] = '\0';
        serial_printf("Set name = %s\r\n", cfg->device_name);
    }
    else if (strcmp(param, "poweron") == 0) {
        if (strcmp(value, "restore") == 0) {
            cfg->power_on_behavior = POWER_ON_RESTORE;
            serial_write("Set poweron = restore (restore last state)\r\n");
        } else if (strcmp(value, "on") == 0) {
            cfg->power_on_behavior = POWER_ON_ON;
            serial_write("Set poweron = on (always turn on)\r\n");
        } else if (strcmp(value, "off") == 0) {
            cfg->power_on_behavior = POWER_ON_OFF;
            serial_write("Set poweron = off (always stay off)\r\n");
        } else {
            serial_write("Error: poweron must be restore, on, or off\r\n");
        }
    }
    else {
        serial_printf("Unknown parameter: %s\r\n", param);
        serial_write("Type 'help' for available parameters\r\n");
    }
}

esp_err_t serial_config_init(void) {
    // Configure USB Serial JTAG
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = SERIAL_BUF_SIZE,
        .rx_buffer_size = SERIAL_BUF_SIZE,
    };

    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB serial driver: %s", esp_err_to_name(err));
        return err;
    }

    // Create the serial config task
    BaseType_t ret = xTaskCreate(
        serial_config_task,
        "serial_config",
        4096,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial config task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serial configuration initialized");
    return ESP_OK;
}

bool serial_config_is_active(void) {
    return s_config_active;
}
