# TLED - Matter-over-Thread LED Controller

## Project Overview
ESP32-C6 based LED controller using Matter protocol over Thread network. Like WLED, but for Thread.

## Current Status: Phase 3 Complete

### What's Working
- **On/Off control** via Home Assistant
- **Brightness control** (0-254 range)
- **HSV color control** - full color picker in HA
- **Thread Router** - device is FTD (Full Thread Device), can route for mesh
- **NVS persistence** - saves power/brightness/color, restores on reboot
- **Factory reset** - hold button on GPIO9 for 5+ seconds
- **Smooth transitions** - 300ms fades on all color/brightness/power changes
- **Built-in effects** - rainbow, breathing, candle, chase (API only for now)

### Hardware Configuration
- **Board:** DFRobot Beetle ESP32-C6 (or any ESP32-C6)
- **LED Strip:** WS2812B on **GPIO5** (10 LEDs configured)
- **Button:** GPIO9 (factory reset)
- **Power:** 5V external supply

## Development Commands

### Build & Flash
```bash
# Source environment
source ~/esp/esp-idf/export.sh && source ~/esp/esp-matter/export.sh

# Build
idf.py build

# Flash (keeps commissioning)
idf.py -p /dev/ttyACM0 flash

# Erase and flash (loses commissioning - need to re-pair)
idf.py -p /dev/ttyACM0 erase-flash && idf.py -p /dev/ttyACM0 flash
```

### Monitor Serial Output
```bash
# Using Python (works without TTY)
/home/maui/.espressif/python_env/idf5.4_py3.13_env/bin/python << 'EOF'
import serial, time
ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1); ser.rts = False  # Reset
start = time.time()
while time.time() - start < 20:
    data = ser.read(1024)
    if data: print(data.decode('utf-8', errors='ignore'), end='', flush=True)
ser.close()
EOF
```

### Get QR Code for Commissioning
After flashing, the QR code URL appears in serial output:
```
https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3AY.K9042C00KA0648G00
```

## Project Structure
```
~/dev/noscope.TLED/
├── main/
│   ├── app_main.cpp      # Matter setup, endpoint creation
│   ├── app_driver.cpp    # LED strip driver, NVS persistence
│   ├── app_driver.h      # Driver interface
│   └── app_config.h      # Pin definitions, LED count
├── docs/
│   └── master-plan.md    # Full project plan (5 phases)
├── sdkconfig.defaults    # ESP-IDF config (Thread FTD, Matter, etc)
└── partitions.csv        # Flash partition layout
```

## Key Implementation Details

### Matter Device Type
Using `dimmable_light` as base + manually added ColorControl cluster with HSV-only feature.
This avoids XY and ColorTemperature modes that caused issues with Home Assistant.

### Color Control
- HSV mode only (no XY, no ColorTemp)
- Hue: 0-254 (Matter range) → 0-360 degrees
- Saturation: 0-254 → 0-100%
- Brightness via LevelControl cluster: 0-254

### NVS Persistence
Saves to `tled_state` namespace:
- `power` (bool)
- `brightness` (uint8)
- `hue` (uint8)
- `saturation` (uint8)

Restored on boot before Matter attributes are applied.

### Thread Configuration
- `CONFIG_OPENTHREAD_FTD=y` - Full Thread Device (can be router)
- Device automatically becomes router when mesh needs it
- No special configuration needed - just keep it powered

### Transition API
```c
// Set brightness with custom transition time
app_driver_light_set_brightness_with_transition(handle, brightness, transition_ms);

// Set color with custom transition time
app_driver_light_set_hsv_with_transition(handle, hue, sat, transition_ms);

// Set an effect (stops normal color mode)
app_driver_light_set_effect(handle, effect_id);
```

## Phase Progress

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | ✅ Done | Basic on/off light over Matter+Thread |
| 2 | ✅ Done | RGB color, brightness, NVS persistence |
| 3 | ✅ Done | Smooth transitions & effects |
| 4 | ⏳ Next | Runtime config (BLE setup for LED count, GPIO) |
| 5 | ⏳ | OTA updates, watchdog, safety features |

## Phase 3 Details - Transitions & Effects

### Transition Engine
- FreeRTOS task running at 50 FPS (20ms tick)
- Smooth HSV interpolation with hue wrap-around handling
- Default 300ms transition time on all changes
- Mutex-protected for thread safety

### Available Effects (API only, no HA integration yet)
```c
app_driver_light_set_effect(handle, TLED_EFFECT_RAINBOW);   // 1 - cycle hues
app_driver_light_set_effect(handle, TLED_EFFECT_BREATHING); // 2 - pulse brightness
app_driver_light_set_effect(handle, TLED_EFFECT_CANDLE);    // 3 - warm flicker
app_driver_light_set_effect(handle, TLED_EFFECT_CHASE);     // 4 - moving dot
app_driver_light_set_effect(handle, TLED_EFFECT_NONE);      // 0 - stop effect
```

## Next Steps (Phase 4)
1. BLE provisioning for runtime configuration
2. Configurable LED count and GPIO pin
3. Configurable default brightness/color

## Known Issues / Notes
- First commission after erase-flash may take a moment
- Thread connection can be weak if far from border router
- After flashing without erase, device keeps its commissioning

## Environment
- ESP-IDF: v5.4.1
- ESP-Matter: main branch
- Target: ESP32-C6
- Matter Device Type: Dimmable Light + Color Control (HSV)
