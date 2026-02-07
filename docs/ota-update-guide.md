# TLED OTA Update Guide

This guide explains how to create and deploy Matter OTA (Over-The-Air) updates for TLED devices via Home Assistant.

## ⚠️ Known Issue: OTA Crashes on ESP32-C6

**Status**: Matter OTA updates currently crash during download on ESP32-C6 devices.

The device crashes with `abort() was called` in `GetMonotonicTimestamp()` after downloading 15-80% of the firmware image. This appears to be a bug in the ESP-Matter SDK's BDX (Bulk Data Exchange) implementation.

**Workarounds tried (none successful)**:
- Increased CHIP task stack (8KB → 16KB)
- Increased Thread/pthread stacks
- Extended watchdog timeout (30s → 120s)
- Reduced firmware size by ~100KB
- Disabled unused clusters

**Recommendation**: Use USB flashing via the web installer until this is resolved upstream. The web installer at `https://[your-pages-url]` works reliably.

**Tracking**: This issue should be reported to [espressif/esp-matter](https://github.com/espressif/esp-matter/issues) when time permits.

---

## Prerequisites

- TLED device already commissioned to Home Assistant via Matter
- Home Assistant with Matter Server add-on
- Access to the Home Assistant file system (e.g., via SSH, Samba, or File Editor add-on)

## Creating an OTA Image

### 1. Build the New Firmware

Update the version in `CMakeLists.txt`:

```cmake
set(PROJECT_VER "0.6.2")       # Human-readable version string
set(PROJECT_VER_NUMBER 8)       # Must be higher than current for OTA to detect
```

Then build:

```bash
source ~/esp/esp-idf/export.sh && source ~/esp/esp-matter/export.sh
idf.py build
```

### 2. Create the Matter OTA Image

The Matter OTA image wraps the firmware binary with metadata. Use the `ota_image_tool.py` from the Matter SDK:

```bash
cd build
python $ESP_MATTER_PATH/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py create \
    -v 65521 \           # Vendor ID (FFF1 in decimal = 65521)
    -p 32768 \           # Product ID (8000 in hex = 32768)
    -vn 8 \              # Software version number (must match PROJECT_VER_NUMBER)
    -vs "0.6.2" \        # Version string (must match PROJECT_VER)
    -da sha256 \         # Digest algorithm
    tled.bin \           # Input firmware binary
    tled-0.6.2.ota       # Output OTA image
```

### 3. Calculate the Checksum

Home Assistant needs a base64-encoded SHA256 checksum of the OTA file:

```bash
openssl dgst -sha256 -binary tled-0.6.2.ota | base64
```

Example output: `fUFpssWxFQFntRYtCCcu7yPK4Tu5xiL9MRlj2QEgdaA=`

### 4. Create the JSON Metadata File

Create a file named `FFF1-8000.json` (format: `{VendorID}-{ProductID}.json` in hex):

```json
{
  "modelVersion": {
    "vid": 65521,
    "pid": 32768,
    "softwareVersion": 8,
    "softwareVersionString": "0.6.2",
    "cdVersionNumber": 1,
    "softwareVersionValid": true,
    "otaUrl": "file:///tled-0.6.2.ota",
    "otaChecksum": "fUFpssWxFQFntRYtCCcu7yPK4Tu5xiL9MRlj2QEgdaA=",
    "otaChecksumType": 1,
    "minApplicableSoftwareVersion": 1,
    "maxApplicableSoftwareVersion": 7
  }
}
```

**Important fields:**
- `softwareVersion`: Must be higher than device's current version
- `softwareVersionString`: Human-readable version
- `otaUrl`: Path to OTA file (relative to Matter Server's OTA directory)
- `otaChecksum`: Base64-encoded SHA256 of the .ota file
- `minApplicableSoftwareVersion`: Minimum device version that can update
- `maxApplicableSoftwareVersion`: Maximum device version that can update (must be < new version)

## Deploying to Home Assistant

### 1. Copy Files to Home Assistant

Copy both files to the Matter Server's OTA directory:
- Path: `/addon_configs/core_matter_server/updates/`

Using SSH:
```bash
scp FFF1-8000.json tled-0.6.2.ota root@homeassistant:/addon_configs/core_matter_server/updates/
```

Or use the Samba share / File Editor add-on to copy files.

### 2. Restart the Matter Server

Go to Settings → Add-ons → Matter Server → Restart

### 3. Trigger the Update

Matter devices check for updates periodically (default: 24 hours). To trigger immediately:

1. Go to Developer Tools → Services
2. Select `update.install`
3. Choose your TLED device's firmware entity
4. Call the service

Or use the UI: Go to the device, find the Firmware update entity, and click "Install".

## Troubleshooting

### Update Not Detected

- Ensure JSON filename matches `{VID}-{PID}.json` format (hex, uppercase)
- Verify `softwareVersion` is higher than device's current version
- Check `maxApplicableSoftwareVersion` includes the device's current version
- Restart Matter Server after copying files

### Device Crashes During OTA

The CHIP task needs sufficient stack space for OTA. Ensure this is set in `sdkconfig.defaults`:

```
CONFIG_CHIP_TASK_STACK_SIZE=12288
```

### Checksum Mismatch

Regenerate the checksum and ensure you're using the correct .ota file:

```bash
openssl dgst -sha256 -binary tled-0.6.2.ota | base64
```

## Quick Reference

```bash
# Full OTA creation workflow
cd ~/dev/noscope.TLED

# 1. Update version in CMakeLists.txt, then:
idf.py build

# 2. Create OTA image
cd build
python $ESP_MATTER_PATH/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py create \
    -v 65521 -p 32768 -vn VERSION_NUMBER -vs "VERSION_STRING" -da sha256 \
    tled.bin tled-VERSION_STRING.ota

# 3. Get checksum
openssl dgst -sha256 -binary tled-VERSION_STRING.ota | base64

# 4. Update FFF1-8000.json with new version and checksum

# 5. Copy to Home Assistant
scp FFF1-8000.json tled-VERSION_STRING.ota root@homeassistant:/addon_configs/core_matter_server/updates/

# 6. Restart Matter Server and trigger update
```
