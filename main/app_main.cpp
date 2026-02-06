/*
 * TLED - Matter-over-Thread LED Controller
 * Main Application Entry Point
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>

#include "app_config.h"
#include "app_driver.h"
#include "app_nvs_config.h"
#include "app_ble_config.h"
#include <app_reset.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

// Version string from CMakeLists.txt
#ifndef PROJECT_VER
#define PROJECT_VER "0.2.0"
#endif

static const char *TAG = "tled_main";

// Global endpoint ID for the light
uint16_t light_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

// Check if boot button is held
static bool is_button_held(uint32_t hold_time_ms)
{
    gpio_num_t btn_gpio = (gpio_num_t)TLED_BOOT_BUTTON_GPIO;

    // Configure GPIO as input with pullup
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << btn_gpio);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Check if button is pressed (active low)
    if (gpio_get_level(btn_gpio) != 0) {
        return false;  // Not pressed
    }

    // Wait and check if still held
    ESP_LOGI(TAG, "Button pressed, waiting %lu ms to confirm config mode...", (unsigned long)hold_time_ms);

    uint32_t held_time = 0;
    const uint32_t check_interval = 100;

    while (held_time < hold_time_ms) {
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        held_time += check_interval;

        if (gpio_get_level(btn_gpio) != 0) {
            ESP_LOGI(TAG, "Button released, normal boot");
            return false;
        }
    }

    ESP_LOGI(TAG, "Button held for %lu ms - entering config mode", (unsigned long)hold_time_ms);
    return true;
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
                chip::CommissioningWindowManager &commissionMgr =
                    chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen()) {
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
                        kTimeoutSeconds,
                        chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR) {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
            break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type,
                                        uint16_t endpoint_id,
                                        uint8_t effect_id,
                                        uint8_t effect_variant,
                                        void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    // TODO: Implement visual identification (e.g., blink LED)
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                          uint16_t endpoint_id,
                                          uint32_t cluster_id,
                                          uint32_t attribute_id,
                                          esp_matter_attr_val_t *val,
                                          void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "TLED - Matter-over-Thread LED Controller");
    ESP_LOGI(TAG, "Version: %s", PROJECT_VER);
    ESP_LOGI(TAG, "==================================");

    /* Initialize the ESP NVS layer */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialize configuration */
    err = tled_config_init();
    ESP_ERROR_CHECK(err);

    const tled_config_t *config = tled_config_get();

    /* Check if first boot - use defaults and save */
    if (!tled_config_is_configured()) {
        ESP_LOGI(TAG, "First boot detected - using default config");
        ESP_LOGI(TAG, "To change settings: hold BOOT button for 5s during startup");

        // Save the default config so next boot isn't "first boot"
        tled_config_save();
    }

    // TODO: BLE config mode on button hold (needs to be done before Matter starts BLE)
    // For now, just log if button is held
    if (is_button_held(3000)) {
        ESP_LOGW(TAG, "Button held - BLE config not yet implemented");
        ESP_LOGW(TAG, "Use nRF Connect to configure via BLE characteristics");
    }

    ESP_LOGI(TAG, "Using config: %d LEDs on GPIO%d", config->num_leds, config->gpio_pin);

    /* Initialize drivers */
    app_driver_handle_t light_handle = app_driver_light_init();
    ABORT_APP_ON_FAILURE(light_handle != NULL, ESP_LOGE(TAG, "Failed to initialize light driver"));

    app_driver_handle_t button_handle = app_driver_button_init();
    ABORT_APP_ON_FAILURE(button_handle != NULL, ESP_LOGE(TAG, "Failed to initialize button driver"));
    app_reset_button_register(button_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* Create Dimmable Light + HSV Color Control
     * Using dimmable_light as base, then adding ColorControl with HSV only
     * This avoids XY and ColorTemperature features that cause issues
     */
    dimmable_light::config_t light_config;
    light_config.on_off.on_off = TLED_DEFAULT_POWER;
    light_config.on_off_lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = TLED_DEFAULT_BRIGHTNESS;
    light_config.level_control.on_level = TLED_DEFAULT_BRIGHTNESS;
    light_config.level_control_lighting.start_up_current_level = nullptr;

    endpoint_t *endpoint = dimmable_light::create(node, &light_config, ENDPOINT_FLAG_NONE, light_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create dimmable light endpoint"));

    /* Add ColorControl cluster with HSV feature only */
    cluster::color_control::config_t color_config;
    color_config.color_mode = static_cast<uint8_t>(ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation);
    color_config.enhanced_color_mode = static_cast<uint8_t>(ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation);
    color_config.color_capabilities = 1;  // Hue/Saturation supported
    cluster_t *color_cluster = cluster::color_control::create(endpoint, &color_config, CLUSTER_FLAG_SERVER);

    /* Add only HSV feature */
    cluster::color_control::feature::hue_saturation::config_t hs_config;
    hs_config.current_hue = 0;
    hs_config.current_saturation = 254;
    cluster::color_control::feature::hue_saturation::add(color_cluster, &hs_config);

    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Color Light (HSV) created with endpoint_id %d", light_endpoint_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    /* Start Matter */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    ESP_LOGI(TAG, "Matter started successfully");

    /* Print commissioning QR code */
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    /* Apply default light settings from Matter attributes */
    app_driver_light_set_defaults(light_endpoint_id);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif

    ESP_LOGI(TAG, "TLED initialization complete. Waiting for commissioning...");

    /* Main loop - just idle, all work done in callbacks */
    while (true) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
