# TLED - Matter-over-Thread LED Controller

A Matter-compatible LED strip controller for ESP32-C6 that works over Thread networking. Control addressable LED strips (WS2812B, SK6812, etc.) from Home Assistant, Apple Home, Google Home, or any Matter-compatible smart home platform.

## Features

- **Matter over Thread** - Native Matter protocol, no cloud or WiFi required
- **Full RGB control** - Color picker, brightness, on/off from your smart home app
- **Smooth transitions** - 300ms fades on all changes
- **Thread mesh networking** - Self-healing network, device acts as a router
- **Web-based installer** - Flash firmware directly from your browser
- **USB configuration** - Change settings via serial without recompiling
- **NVS persistence** - Settings survive reboots

## Hardware Requirements

- **ESP32-C6 board** - Any board with USB-C (e.g., DFRobot Beetle ESP32-C6, ESP32-C6-DevKitC)
- **Addressable LED strip** - WS2812B, WS2811, or SK6812
- **5V power supply** - Size for your LED count (~60mA per LED at full white)
- **Thread border router** - HomePod Mini, Apple TV 4K, Google Nest Hub, or dedicated like SLZB-06/SMLight

## Quick Start

### 1. Flash the Firmware

Visit the **[Web Installer](https://maui1911.github.io/TLED)** and click "Install TLED Firmware".

> **Note:** Requires Chrome or Edge browser. If prompted, hold the BOOT button on your ESP32-C6 while clicking Install.

### 2. Configure Your LED Strip

1. Go to the **Configure** tab in the web installer
2. Click **Connect to Device**
3. Set your LED count, GPIO pin, and LED type
4. Click **Save & Reboot**

### 3. Commission to Your Smart Home

After reboot, a QR code will appear in the web installer. Scan it with:
- **Home Assistant** - Settings → Devices & Services → Add Integration → Matter
- **Apple Home** - Add Accessory → Scan QR Code
- **Google Home** - Add Device → Matter-enabled device

## Configuration Options

| Setting | Default | Description |
|---------|---------|-------------|
| LED Count | 10 | Number of LEDs in your strip (1-1000) |
| GPIO Pin | 5 | Data pin connected to LED strip |
| LED Type | WS2812B | Chipset: WS2812B, WS2811, or SK6812 |
| RGB Order | GRB | Color byte order (try others if colors are wrong) |
| Max Brightness | 255 | Limits maximum brightness (saves power) |

## Wiring

```
ESP32-C6          LED Strip
─────────         ─────────
GPIO 5    ────────  DIN (Data In)
GND       ────────  GND
                    5V  ──── External 5V Power Supply
```

> **Important:** Power your LED strip from an external 5V supply, not from the ESP32's 5V pin (except for very short strips).

## Serial Commands

Connect via USB and use the serial console in the web installer, or any serial terminal at 115200 baud:

```
help                    Show available commands
config                  Show current configuration
set leds <n>            Set number of LEDs
set gpio <n>            Set GPIO pin
set type <type>         Set LED type (ws2812b/ws2811/sk6812)
set order <order>       Set RGB order (grb/rgb/bgr/rbg)
set brightness <1-255>  Set max brightness
save                    Save configuration to flash
reboot                  Restart device
factory                 Factory reset (erases settings & commissioning)
```

## Building from Source

### Prerequisites

- [ESP-IDF v5.4+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/)
- [ESP-Matter](https://github.com/espressif/esp-matter)

### Build & Flash

```bash
# Source the environments
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh

# Build
idf.py build

# Flash (keeps commissioning data)
idf.py -p /dev/ttyACM0 flash

# Flash with erase (clears commissioning - need to re-pair)
idf.py -p /dev/ttyACM0 erase-flash flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor
```

### Configuration via menuconfig

```bash
idf.py menuconfig
# Navigate to "TLED Configuration" for build-time defaults
```

## Troubleshooting

### Can't flash the device
- Use Chrome or Edge (Firefox doesn't support Web Serial)
- Hold the BOOT button while clicking Install
- Try a different USB cable (some are charge-only)

### "No bootable app partitions" / Boot loop
- Flash was interrupted. Try flashing again.

### Can't find device during commissioning
- Commission within 30 seconds of boot (BLE advertising slows down)
- Run `factory` command if device was previously commissioned
- Move closer to your Thread border router

### Wrong colors
- Try different RGB Order settings (GRB → RGB → BGR → RBG)

### LEDs don't light up
- Check 5V power supply connection
- Verify GPIO pin matches your wiring
- Confirm LED count is correct

## Project Structure

```
TLED/
├── main/
│   ├── app_main.cpp          # Matter setup, endpoint creation
│   ├── app_driver.cpp        # LED strip driver, transitions
│   ├── app_nvs_config.cpp    # Runtime configuration storage
│   ├── app_serial_config.cpp # USB serial command interface
│   └── Kconfig.projbuild     # Build-time configuration options
├── web-installer/
│   ├── index.html            # Web installer & configurator
│   └── manifest.json         # ESP Web Tools manifest
├── partitions.csv            # Flash partition layout
└── sdkconfig.defaults        # Default SDK configuration
```

## Roadmap

- [x] Basic on/off control via Matter
- [x] RGB color control (HSV)
- [x] Brightness control
- [x] Smooth transitions
- [x] NVS persistence
- [x] Web installer
- [x] Serial configuration
- [ ] OTA updates via Matter
- [ ] Built-in effects (rainbow, breathing, etc.) exposed to Matter
- [ ] Multiple segments/zones

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [ESP-Matter](https://github.com/espressif/esp-matter) - Espressif's Matter SDK
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) - Browser-based flashing
- [ConnectedHomeIP](https://github.com/project-chip/connectedhomeip) - Matter protocol implementation
