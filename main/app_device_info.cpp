/*
 * TLED - Custom Device Instance Info Provider
 * Sets proper device identification for Matter
 */

#include "app_device_info.h"
#include <platform/ESP32/ESP32Config.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <lib/support/CodeUtils.h>
#include <cstring>
#include <esp_log.h>
#include <esp_matter_providers.h>

static const char* TAG = "tled_devinfo";

namespace tled {

// Device identification constants
static constexpr char kVendorName[] = "TLED";
static constexpr char kProductName[] = "Matter LED Controller";
static constexpr char kHardwareVersionString[] = "ESP32-C6";
static constexpr char kProductURL[] = "https://github.com/maui1911/TLED";
static constexpr char kProductLabel[] = "TLED";
static constexpr uint16_t kVendorId = 0xFFF1;  // Test vendor ID
static constexpr uint16_t kProductId = 0x8000;
static constexpr uint16_t kHardwareVersion = 1;

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetVendorName(char * buf, size_t bufSize)
{
    ESP_LOGI(TAG, "GetVendorName called, returning: %s", kVendorName);
    VerifyOrReturnError(buf != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(bufSize >= sizeof(kVendorName), CHIP_ERROR_BUFFER_TOO_SMALL);
    strcpy(buf, kVendorName);
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetVendorId(uint16_t & vendorId)
{
    vendorId = kVendorId;
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetProductName(char * buf, size_t bufSize)
{
    ESP_LOGI(TAG, "GetProductName called, returning: %s", kProductName);
    VerifyOrReturnError(buf != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(bufSize >= sizeof(kProductName), CHIP_ERROR_BUFFER_TOO_SMALL);
    strcpy(buf, kProductName);
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetProductId(uint16_t & productId)
{
    productId = kProductId;
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetPartNumber(char * buf, size_t bufSize)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetProductURL(char * buf, size_t bufSize)
{
    VerifyOrReturnError(buf != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(bufSize >= sizeof(kProductURL), CHIP_ERROR_BUFFER_TOO_SMALL);
    strcpy(buf, kProductURL);
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetProductLabel(char * buf, size_t bufSize)
{
    VerifyOrReturnError(buf != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(bufSize >= sizeof(kProductLabel), CHIP_ERROR_BUFFER_TOO_SMALL);
    strcpy(buf, kProductLabel);
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetSerialNumber(char * buf, size_t bufSize)
{
    // Use chip's default serial number from MAC address
    return chip::DeviceLayer::Internal::ESP32Config::ReadConfigValueStr(
        chip::DeviceLayer::Internal::ESP32Config::kConfigKey_SerialNum, buf, bufSize, bufSize);
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetHardwareVersion(uint16_t & hardwareVersion)
{
    hardwareVersion = kHardwareVersion;
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetHardwareVersionString(char * buf, size_t bufSize)
{
    VerifyOrReturnError(buf != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(bufSize >= sizeof(kHardwareVersionString), CHIP_ERROR_BUFFER_TOO_SMALL);
    strcpy(buf, kHardwareVersionString);
    return CHIP_NO_ERROR;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetRotatingDeviceIdUniqueId(chip::MutableByteSpan & uniqueIdSpan)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetProductFinish(chip::app::Clusters::BasicInformation::ProductFinishEnum * finish)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR TLEDDeviceInstanceInfoProvider::GetProductPrimaryColor(chip::app::Clusters::BasicInformation::ColorEnum * primaryColor)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

// Global instance
static TLEDDeviceInstanceInfoProvider sDeviceInfoProvider;

void set_device_info_provider()
{
    ESP_LOGI(TAG, "Setting custom TLED device info provider");
    esp_matter::set_custom_device_instance_info_provider(&sDeviceInfoProvider);
}

} // namespace tled
