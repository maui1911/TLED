# TLED — Matter-over-Thread LED Controller

## Like WLED, but for Thread.

---

## Project Overview

TLED is a custom Matter-over-Thread firmware for the **DFRobot Beetle ESP32-C6** that turns it into a configurable addressable LED strip controller. The device appears as a standard Matter light in Home Assistant (or any Matter controller), supporting full RGB, brightness, and eventually configurable strip parameters — all over Thread, no WiFi needed.

**Target Hardware:**
- DFRobot Beetle ESP32-C6 (v1.1)
- WS2812B / WS2811 / SK6812 addressable LED strips
- External 5V power supply (shared between ESP32 VIN and strip)

**Wiring:**
```
5V Power Adapter ──┬──► Beetle VIN (5V)
                   └──► LED Strip VCC

Beetle GND ────────┬──► LED Strip GND
                   └──► Power Supply GND

Beetle GPIO4 ──────────► LED Strip DATA
```

---

## Project Paths

| Location | Purpose |
|----------|---------|
| `~/dev/noscope.TLED/` | **TLED firmware project root** (all source code, config, build output) |
| `~/esp/esp-idf/` | ESP-IDF v5.4.1 toolchain (shared, do not modify) |
| `~/esp/esp-matter/` | ESP-Matter SDK (shared, do not modify) |

> **Important:** The TLED firmware lives in `~/dev/noscope.TLED/`, separate from the ESP toolchains in `~/esp/`. This keeps the project cleanly isolated and easy to manage with Git/Claude Code.

---

## Tech Stack & Versions

> **Important:** ESP-Matter SDK has specific version requirements. Do not use the latest ESP-IDF — use the version recommended by ESP-Matter for ESP32-C6.

| Component | Version | Notes |
|-----------|---------|-------|
| **ESP-IDF** | **v5.4.1** | Recommended by ESP-Matter for ESP32-C6 |
| **ESP-Matter SDK** | **main branch** (component `^1.4.0`) | Espressif's official Matter implementation |
| **connectedhomeip** | commit `8f943388af` | Pinned by ESP-Matter SDK |
| **Matter Spec** | 1.4 | Latest supported by ESP-Matter |
| **Language** | C/C++ | |
| **Build System** | CMake (ESP-IDF standard) | |
| **Target** | ESP32-C6 | RISC-V, 160MHz, 512KB SRAM, 4MB Flash |

> **Note:** ESP-IDF v5.5.2 is the latest stable ESP-IDF release, but ESP-Matter SDK explicitly recommends v5.4.1 for the ESP32-C6. Using a different version may cause build failures or runtime issues. Always follow ESP-Matter's recommendation.

---

## Repository Structure

```
~/dev/noscope.TLED/
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.cpp          # Entry point, Matter + Thread init
│   ├── app_driver.h          # LED strip driver header
│   ├── app_driver.cpp        # RMT / LED strip driver implementation
│   ├── app_matter.h          # Matter device setup header
│   ├── app_matter.cpp        # Matter clusters, attributes, callbacks
│   └── app_config.h          # Configuration constants
├── partitions.csv             # Custom partition table for Matter
├── sdkconfig.defaults         # Default ESP-IDF config (Thread, Matter, RMT)
├── CMakeLists.txt             # Top-level CMake
└── README.md
```

Later phases will add:
```
│   ├── app_nvs_config.h       # NVS configuration management header
│   ├── app_nvs_config.cpp     # Runtime config stored in NVS
│   ├── app_effects.h          # LED effects header
│   └── app_effects.cpp        # Built-in LED effects
```

---

## Development Environment Setup

Before starting Phase 1, set up the development environment. This project is developed on **Windows using WSL** (Ubuntu) or natively on Linux.

### Prerequisites

Install required system packages (Ubuntu/WSL):
```bash
sudo apt-get install git gcc g++ pkg-config libssl-dev libdbus-1-dev \
  libglib2.0-dev libavahi-client-dev ninja-build python3-venv python3-dev \
  python3-pip unzip libgirepository1.0-dev libcairo2-dev libreadline-dev
```

### 1. Install ESP-IDF v5.4.1

```bash
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4.1
git submodule update --init --recursive
./install.sh esp32c6
source ./export.sh
cd ..
```

### 2. Install ESP-Matter SDK

```bash
cd ~/esp
git clone --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
cd ./connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow
cd ../..
./install.sh
source ./export.sh
cd ..
```

### 3. Set up shell aliases (add to ~/.bashrc)

```bash
# ESP-IDF environment
alias get_idf='. ~/esp/esp-idf/export.sh'

# ESP-Matter environment (sources IDF automatically)
alias get_matter='. ~/esp/esp-idf/export.sh && . ~/esp/esp-matter/export.sh'

# Enable ccache for faster builds (Matter builds are slow)
alias set_cache='export IDF_CCACHE_ENABLE=1'
```

### 4. Verify environment

```bash
source ~/.bashrc
get_matter
set_cache
idf.py --version
# Should show: ESP-IDF v5.4.1
```

### 5. Test with ESP-Matter light example first

```bash
cd ~/esp/esp-matter/examples/light
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

If this example builds, flashes, and shows a Matter commissioning QR code in the serial output, the environment is ready.

> **Windows USB note:** If using WSL, you need `usbipd-win` to forward USB devices. Install it from https://github.com/dorssel/usbipd-win and use `usbipd attach --wsl` to make the Beetle visible in WSL.

---

## Phase 1: Foundation — On/Off Light over Matter + Thread

**Goal:** ESP32-C6 boots, joins Thread network via Matter commissioning, appears as a basic light in Home Assistant, on/off works.

### Tasks

1. **Create project scaffolding**
   - Set up the `~/dev/noscope.TLED/` repo structure shown above
   - Configure `CMakeLists.txt` for ESP-Matter
   - Create `partitions.csv` with enough space for Matter + Thread (needs OTA partition, NVS, factory data, etc.)
   - Set up `sdkconfig.defaults`:
     - Enable IEEE 802.15.4 (Thread)
     - Enable Matter
     - Enable RMT peripheral
     - Set flash size to 4MB
     - Disable WiFi (not needed)
     - Enable USB Serial/JTAG for console output

2. **Implement `app_main.cpp`**
   - Initialize NVS flash
   - Initialize Matter stack via ESP-Matter SDK APIs
   - Create a Matter "On/Off Light" device (Device Type 0x0100)
   - Set up Thread networking
   - Start Matter commissioning (generates QR code for pairing)
   - Log commissioning QR code URL to serial monitor

3. **Implement `app_matter.cpp`**
   - Create Matter node with a single endpoint
   - Add required clusters for On/Off Light:
     - On/Off cluster (0x0006)
     - Descriptor cluster
     - Basic Information cluster (device name: "TLED", vendor info, etc.)
   - Register attribute change callback for on/off
   - When on/off changes → call driver to toggle LED

4. **Implement `app_driver.cpp`**
   - For this phase: just toggle the **onboard LED (GPIO15)** based on on/off state
   - No strip driving yet — keep it simple to isolate Matter/Thread issues
   - Function signatures:
     ```cpp
     void app_driver_init();
     void app_driver_set_power(bool on);
     ```

5. **Test commissioning flow**
   - Flash the firmware via USB
   - Read QR code from serial log
   - In Home Assistant: **Settings → Devices → Add Device → Matter** → scan QR code
   - Verify the device shows up as a light named "TLED"
   - Toggle on/off from HA dashboard
   - Verify onboard LED responds

### Success Criteria
- [ ] Device appears in Home Assistant as a Matter light
- [ ] On/off toggle from HA controls the onboard LED
- [ ] Device survives reboot and reconnects to Thread network automatically

### Key References
- ESP-Matter light example: `~/esp/esp-matter/examples/light/`
- ESP-Matter programming guide: https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/
- Matter On/Off Light device type spec: Device Type 0x0100
- ESP-IDF Thread documentation for ESP32-C6

---

## Phase 2: RGB Light with LED Strip

**Goal:** Upgrade to Extended Color Light with full RGB control driving an actual WS2812B strip via RMT.

### Tasks

1. **Change Matter device type**
   - Switch from On/Off Light (0x0100) to Extended Color Light (0x010D)
   - Add required clusters:
     - Level Control cluster (0x0008) — brightness
     - Color Control cluster (0x0300) — hue, saturation, XY color
   - Register callbacks for level and color attribute changes

2. **Implement LED strip driver in `app_driver.cpp`**
   - Initialize RMT peripheral for WS2812B on GPIO4
   - Use ESP-IDF's `led_strip` component (available in ESP-IDF v5.4.1)
   - Functions to implement:
     ```cpp
     void app_driver_init();
     void app_driver_set_power(bool on);
     void app_driver_set_brightness(uint8_t level);
     void app_driver_set_color_hsv(uint8_t hue, uint8_t saturation);
     void app_driver_set_color_xy(uint16_t x, uint16_t y);
     void app_driver_update_strip();
     ```
   - Convert Matter HSV/XY values to RGB for WS2812B
   - Apply brightness and color to all LEDs uniformly
   - Hardcode for now: `NUM_LEDS = 250`, `GPIO = 4`, `RGB_ORDER = GRB`

3. **Handle color modes**
   - Matter can send color as HSV or XY — implement both conversions to RGB
   - Store current color state so it persists correctly between mode switches

4. **Persist state in NVS**
   - Save last on/off, brightness, and color to NVS
   - Restore on boot (power-on behavior: restore last state)

### Success Criteria
- [ ] Full RGB color picker works from Home Assistant
- [ ] Brightness slider works smoothly
- [ ] 250 LED strip lights up with correct colors
- [ ] State survives power cycles (restores last color/brightness)

### Key References
- Matter Extended Color Light spec: Device Type 0x010D
- ESP-IDF `led_strip` component: https://components.espressif.com/components/espressif/led_strip
- Matter Color Control cluster spec (0x0300)
- Matter Level Control cluster spec (0x0008)

---

## Phase 3: Smooth Transitions & Effects

**Goal:** Smooth fading between states and basic built-in effects.

### Tasks

1. **Implement transition support**
   - Matter's Level Control and Color Control clusters support a `TransitionTime` field
   - Implement a non-blocking transition engine:
     - Lerp from current color/brightness to target over N milliseconds
     - Use a FreeRTOS task or ESP timer for smooth updates
   - Target: ~50 updates/second for visually smooth transitions

2. **Add built-in effects in `app_effects.cpp`**
   - Rainbow cycle
   - Breathing / pulse
   - Color wipe
   - Solid color (default — no effect)
   - Effects selectable via a custom Matter attribute on a vendor-specific cluster

3. **Smooth power on/off**
   - Fade in when turning on (instead of instant snap)
   - Fade out when turning off

### Success Criteria
- [ ] Color and brightness changes fade smoothly
- [ ] At least 3 built-in effects available
- [ ] No visible flickering or stuttering during transitions
- [ ] Effects controllable from HA (even if via developer tools initially)

---

## Phase 4: Easy Configuration & Web Installer

**Goal:** Make TLED dead simple to set up — no command line, no ESP-IDF installation needed.

### Two-Tier Configuration Approach

**Tier 1: Web Installer (for end users)**
- Visit `tled.github.io` in Chrome/Edge
- Select your settings from dropdowns
- Click "Install" → firmware flashes directly over USB
- Done!

**Tier 2: Kconfig/menuconfig (for developers)**
- Run `idf.py menuconfig`
- Navigate to "TLED Configuration"
- Set LED count, GPIO, brightness, etc.
- Build and flash

### Configurable Parameters

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| LED Count | 10 | 1-1000 | Number of LEDs in strip |
| GPIO Pin | 5 | 0-21 | Data pin (avoid 9, 12-13, 15) |
| Max Brightness | 255 | 1-255 | Safety limit for power supply |
| LED Type | WS2812B | WS2812B/WS2811/SK6812 | Chipset timing |
| RGB Order | GRB | GRB/RGB/BRG/etc | Color byte order |
| Transition Time | 300ms | 0-5000ms | Fade duration |

### Web Installer Implementation

Uses **ESP Web Tools** (https://esphome.github.io/esp-web-tools/) — same tech as WLED, ESPHome.

**How it works:**
1. User visits webpage hosted on GitHub Pages
2. Webpage shows configuration form
3. User selects: LED count, GPIO, brightness, LED type
4. Clicks "Install TLED"
5. Browser prompts for USB device selection
6. Firmware flashes directly — no drivers, no software install
7. Device reboots, ready to commission in Home Assistant

**Technical approach:**
- Pre-build firmware binaries for common configurations, OR
- Build a single firmware with "factory defaults" that user can later change via NVS, OR
- Use a manifest.json that points to the right binary based on user selection

**Webpage structure:**
```
tled.github.io/
├── index.html          # Main installer page
├── manifest.json       # ESP Web Tools manifest
├── firmware/
│   └── tled.bin        # Latest firmware binary
└── static/
    ├── style.css
    └── logo.png
```

**Sample HTML:**
```html
<esp-web-install-button manifest="manifest.json">
  <button slot="activate">Install TLED</button>
</esp-web-install-button>

<script type="module" src="https://unpkg.com/esp-web-tools/dist/web/install-button.js"></script>
```

### Kconfig Menu (Already Implemented)

Located in `main/Kconfig.projbuild`:

```
TLED Configuration
├── Number of LEDs in strip (1-1000)
├── LED data GPIO pin (0-21)
├── Maximum brightness (0-255)
├── LED strip type (WS2812B / WS2811 / SK6812)
├── RGB byte order (GRB / RGB / BRG / ...)
└── Default transition time (0-5000ms)
```

Developers run `idf.py menuconfig` → set values → build → flash.

### Implementation Tasks

1. **Kconfig integration** ✅ DONE
   - Created `main/Kconfig.projbuild` with all config options
   - Config values used at compile time via `CONFIG_TLED_*` macros

2. **NVS config persistence** ✅ DONE
   - `app_nvs_config.cpp` saves/loads config to NVS
   - First boot uses Kconfig defaults, saves to NVS
   - Subsequent boots load from NVS

3. **Web Installer page**
   - Create GitHub repo or GitHub Pages site
   - Build ESP Web Tools installer page
   - Add configuration dropdowns
   - Host pre-built firmware binaries
   - Test on Chrome, Edge, Android

4. **CI/CD for firmware builds**
   - GitHub Actions workflow to build firmware on release
   - Upload binary artifacts to releases
   - Update manifest.json automatically

### Success Criteria
- [x] Kconfig menu works via `idf.py menuconfig`
- [x] Config persists in NVS across reboots
- [x] Driver uses config values for GPIO, LED count, etc.
- [ ] Web installer page hosted and working
- [ ] End user can flash without any command-line tools
- [ ] Works on Chrome, Edge, Android Chrome

---

## Phase 5: Production Polish

**Goal:** Reliable enough for permanent "set and forget" deployment.

### Tasks

1. **OTA Updates**
   - Implement Matter OTA provider support
   - Allow firmware updates via Home Assistant or any Matter controller
   - Dual partition scheme for safe rollback on failed update

2. **Power-on behavior settings**
   - Configurable options (stored in NVS):
     - Restore last state (default)
     - Always on at specific color/brightness
     - Always off
   - Selectable via BLE config or Matter custom attribute

3. **Reliability**
   - Watchdog timer — auto-reboot if firmware hangs
   - Crash recovery — graceful restart after panic
   - Thread network recovery — auto-rejoin if disconnected
   - Memory monitoring — log free heap, detect leaks over long runtime

4. **Safety**
   - Max brightness limiting (prevent overloading cheap power supplies)
   - Optional: estimate current draw based on LED count × brightness, warn if exceeding configured PSU rating
   - Thermal monitoring via ESP32-C6 internal temperature sensor

5. **Matter certification prep** (optional, for if this becomes a real product)
   - Proper Vendor ID / Product ID registration
   - Device attestation certificates (DAC)
   - Compliance testing against Matter spec
   - Note: Full certification costs money — not needed for personal/hobby use

6. **TLED Enclosure**
   - Design parametric enclosure (OpenSCAD or FreeCAD)
   - Ventilation holes for heat dissipation
   - USB-C access hole for emergency reflashing
   - Mounting options (screw holes, adhesive pad, DIN rail clip)
   - Strain relief for wires
   - Designed to fit Beetle ESP32-C6 v1.1 form factor (25×20.5mm)

### Success Criteria
- [ ] Runs stable for weeks without intervention
- [ ] OTA updates work reliably
- [ ] Survives power outages gracefully (restores state)
- [ ] Safe under all operating conditions

---

## Implementation Notes

### Matter Commissioning Flow
1. TLED boots, starts IEEE 802.15.4 radio and Thread interface
2. TLED enters commissioning mode (indicated by onboard LED pattern)
3. User scans QR code with Home Assistant companion app or other Matter controller
4. Controller provisions Thread credentials and Matter fabric to TLED
5. TLED joins Thread network and becomes controllable
6. On subsequent boots, TLED auto-joins Thread network — no re-commissioning needed

### Memory Budget (ESP32-C6: 512KB SRAM, 4MB Flash)

| Component | RAM Usage | Notes |
|-----------|-----------|-------|
| Matter stack | ~100-150KB | Largest consumer |
| Thread stack | ~50-80KB | |
| LED pixel buffer | ~750 bytes | 250 LEDs × 3 bytes RGB |
| App logic | ~10-20KB | |
| FreeRTOS overhead | ~30-40KB | |
| **Total estimate** | **~250-300KB** | Leaves headroom in 512KB |

Flash partition layout needs to accommodate: app, OTA, NVS, factory data (Matter certificates).

### RMT Driver Notes
- ESP32-C6 RMT peripheral handles WS2812B timing in hardware — no CPU involvement during data transmission
- This means Thread stack won't interfere with LED timing (unlike bit-banging)
- Use ESP-IDF's `led_strip` component, not raw RMT API
- Different chipsets need different timing parameters:

| Chipset | T0H | T0L | T1H | T1L |
|---------|-----|-----|-----|-----|
| WS2812B | 400ns | 850ns | 800ns | 450ns |
| WS2811 | 500ns | 2000ns | 1200ns | 1300ns |
| SK6812 | 300ns | 900ns | 600ns | 600ns |

### Thread vs WiFi — Why Thread?
- Thread is IPv6 only — Matter works natively over IPv6
- No SSID/password needed — Thread credentials provisioned via Matter commissioning
- Mesh networking — TLED devices can relay for each other
- Very low power — relevant for battery-powered future projects
- Requires a Thread Border Router (HA Yellow, SkyConnect, Apple TV, Google Nest Hub, etc.)
- Already proven working with ESPHome on this exact hardware

---

## Quick Reference

| Item | Value |
|------|-------|
| **Project Name** | TLED |
| **Project Path** | `~/dev/noscope.TLED/` |
| **Board** | DFRobot Beetle ESP32-C6 v1.1 |
| **Chip** | ESP32-C6FH4 (RISC-V, 160MHz) |
| **Flash** | 4MB |
| **SRAM** | 512KB |
| **Data Pin** | GPIO4 (soldered) |
| **Power Pin** | VIN (5V from external adapter) |
| **Onboard LED** | GPIO15 |
| **Boot Button** | GPIO9 |
| **LED Strip** | WS2812B (default) |
| **LED Count** | 250 (default, configurable in Phase 4) |
| **RGB Order** | GRB (default, configurable in Phase 4) |
| **Protocol** | Matter over Thread |
| **Matter Device Type** | Extended Color Light (0x010D) |
| **ESP-IDF** | v5.4.1 |
| **ESP-Matter** | main branch, component `^1.4.0` |
| **connectedhomeip** | commit `8f943388af` |

---

## Getting Started (for Claude Code)

Start with **Phase 1**. Run these commands first:

```bash
# 1. Source the environment
get_matter
set_cache

# 2. Create project directory
mkdir -p ~/dev/noscope.TLED
cd ~/dev/noscope.TLED

# 3. Initialize as ESP-IDF project
idf.py create-project tled
idf.py set-target esp32c6
```

Then scaffold the project structure from this plan and implement `app_main.cpp` with a basic Matter On/Off Light device type. Test with the **onboard LED (GPIO15)** before touching the LED strip.

The ESP-Matter light example at `~/esp/esp-matter/examples/light/` is your best reference — study its structure, then adapt for TLED.
