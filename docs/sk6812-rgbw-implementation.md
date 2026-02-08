# SK6812 RGBW Implementation Guide (v0.6.3)

## Problem

The firmware has SK6812 RGBW defined in the configuration system (Kconfig, NVS config enums, serial config commands) but the LED strip driver **always uses the WS2812B 3-byte-per-pixel legacy driver** regardless of chipset selection. This causes byte misalignment on SK6812 RGBW strips.

## Root Cause

`app_driver.cpp` uses the **legacy** `led_strip` API that ships built-in with ESP-IDF:

```cpp
led_strip_t *strip;                                          // Old pointer type with vtable
strip_config = LED_STRIP_DEFAULT_CONFIG(...);                 // Old macro
s_light_driver.strip = led_strip_new_rmt_ws2812(&strip_config); // Old factory - WS2812 only
driver->strip->set_pixel(driver->strip, i, r, g, b);        // 3-byte only
driver->strip->refresh(driver->strip, 100);                  // Has timeout param
driver->strip->clear(driver->strip, 100);                    // Has timeout param
```

This legacy API has **no RGBW support**. There is no `set_pixel_rgbw()` method and no SK6812 factory function.

## Solution: Migrate to Modern `espressif/led_strip` Managed Component

The `espressif/led_strip` managed component (v2.5.x or v3.0.x) supports SK6812 RGBW natively.

### API Comparison

| Legacy (current) | Modern (target) |
|---|---|
| `led_strip_t *strip` | `led_strip_handle_t strip` |
| `LED_STRIP_DEFAULT_CONFIG(num, dev)` | `led_strip_config_t` + `led_strip_rmt_config_t` structs |
| `led_strip_new_rmt_ws2812(&config)` | `led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &handle)` |
| `strip->set_pixel(strip, i, r, g, b)` | `led_strip_set_pixel(handle, i, r, g, b)` |
| N/A | `led_strip_set_pixel_rgbw(handle, i, r, g, b, w)` |
| `strip->refresh(strip, 100)` | `led_strip_refresh(handle)` (no timeout) |
| `strip->clear(strip, 100)` | `led_strip_clear(handle)` (no timeout) |

### Modern API for SK6812 RGBW Creation

```cpp
#include "led_strip.h"

led_strip_handle_t strip;

led_strip_config_t strip_config = {
    .strip_gpio_num = gpio_pin,
    .max_leds = num_leds,
    .led_model = LED_MODEL_SK6812,               // SK6812 timing
    .led_pixel_format = LED_PIXEL_FORMAT_GRBW,   // 4-byte RGBW (v2.x API)
    .flags = { .invert_out = false },
};

led_strip_rmt_config_t rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
    .mem_block_symbols = 64,
    .flags = { .with_dma = false },
};

ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
```

For WS2812B:
```cpp
led_strip_config_t strip_config = {
    .strip_gpio_num = gpio_pin,
    .max_leds = num_leds,
    .led_model = LED_MODEL_WS2812,               // WS2812 timing
    .led_pixel_format = LED_PIXEL_FORMAT_GRB,     // 3-byte RGB (v2.x API)
    .flags = { .invert_out = false },
};
// Same rmt_config, same led_strip_new_rmt_device() call
```

### RGBW Pixel Setting

```cpp
esp_err_t led_strip_set_pixel_rgbw(led_strip_handle_t strip,
    uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);
```

Only call this on strips configured with GRBW format. For RGB strips, use `led_strip_set_pixel()`.

---

## Step-by-Step Implementation

### Step 1: Add managed component dependency

**File: `main/idf_component.yml`**

```yaml
dependencies:
  espressif/button: "^4"
  espressif/led_strip: "^2.5"
  idf:
    version: ">=5.0.0"
```

Use v2.5.x for stability. The v3.x API renamed `led_pixel_format` to `color_component_format` - either works but v2.x is more battle-tested.

**Important:** After adding this, run `idf.py build` once. The managed component will be downloaded to `managed_components/espressif__led_strip/`. Check if the built-in legacy `led_strip` conflicts - you may need to remove the `#include <driver/rmt.h>` since the modern component handles RMT internally.

### Step 2: Add `is_rgbw` flag to driver state

**File: `main/app_driver.cpp` ~line 77**

In `light_driver_t`, add:
```cpp
bool is_rgbw;           // true when chipset is SK6812 (4 bytes per pixel)
```

Change the strip pointer type:
```cpp
led_strip_handle_t strip;   // was: led_strip_t *strip;
```

Update the static initializer (~line 110):
```cpp
.strip = NULL,
.is_rgbw = false,
```

### Step 3: Migrate strip initialization

**File: `main/app_driver.cpp`, function `app_driver_light_init()` (~line 992)**

Remove the old RMT + legacy factory code (lines 1010-1035) and replace with:

```cpp
// Determine if RGBW mode
s_light_driver.is_rgbw = (config->chipset == CHIPSET_SK6812);

// Buffer size: use max of configured LEDs or 100 to clear leftover data
uint16_t strip_buffer_size = s_light_driver.num_leds > 100 ? s_light_driver.num_leds : 100;

// Configure LED strip
led_strip_config_t strip_config = {
    .strip_gpio_num = s_light_driver.gpio_pin,
    .max_leds = strip_buffer_size,
    .led_model = s_light_driver.is_rgbw ? LED_MODEL_SK6812 : LED_MODEL_WS2812,
    .led_pixel_format = s_light_driver.is_rgbw ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB,
    .flags = { .invert_out = false },
};

led_strip_rmt_config_t rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,
    .mem_block_symbols = 64,
    .flags = { .with_dma = false },
};

esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_light_driver.strip);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
    return NULL;
}

ESP_LOGI(TAG, "LED strip created: model=%s, format=%s",
         s_light_driver.is_rgbw ? "SK6812" : "WS2812",
         s_light_driver.is_rgbw ? "GRBW" : "GRB");
```

Also remove `#include <driver/rmt.h>` from the top of the file (line 8) - the modern component handles RMT internally.

### Step 4: Add RGB-to-RGBW white extraction helper

**File: `main/app_driver.cpp`**, add near the other helper functions (after `hsv_to_rgb`, ~line 269):

```cpp
// Extract white component from RGB for RGBW strips
static void rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b,
                         uint8_t *ro, uint8_t *go, uint8_t *bo, uint8_t *wo)
{
    // Extract common white component (min of R,G,B)
    uint8_t w = r < g ? r : g;
    w = w < b ? w : b;
    *ro = r - w;
    *go = g - w;
    *bo = b - w;
    *wo = w;
}
```

### Step 5: Add unified `driver_set_pixel()` helper

**File: `main/app_driver.cpp`**, add after `rgb_to_rgbw`:

```cpp
// Set a single pixel, handling RGBW conversion if needed
static esp_err_t driver_set_pixel(light_driver_t *driver, int index,
                                   uint8_t r, uint8_t g, uint8_t b)
{
    if (driver->is_rgbw) {
        uint8_t ro, go, bo, wo;
        rgb_to_rgbw(r, g, b, &ro, &go, &bo, &wo);
        return led_strip_set_pixel_rgbw(driver->strip, index, ro, go, bo, wo);
    }
    return led_strip_set_pixel(driver->strip, index, r, g, b);
}
```

### Step 6: Update all call sites

Replace every `driver->strip->set_pixel(driver->strip, i, r, g, b)` with `driver_set_pixel(driver, i, r, g, b)`.

Replace every `driver->strip->refresh(driver->strip, 100)` with `led_strip_refresh(driver->strip)`.

Replace every `driver->strip->clear(driver->strip, 100)` with `led_strip_clear(driver->strip)`.

**Specific locations:**

1. **`update_strip_rgb()` (~line 279):**
   ```cpp
   driver_set_pixel(driver, i, r, g, b);
   // ...
   return led_strip_refresh(driver->strip);
   ```

2. **`update_strip_array()` (~line 293):**
   ```cpp
   driver_set_pixel(driver, i, colors[i][0], colors[i][1], colors[i][2]);
   // ...
   return led_strip_refresh(driver->strip);
   ```

3. **`effect_chase()` (~lines 444, 446, 448):**
   ```cpp
   driver_set_pixel(driver, i, r, g, b);        // main dot
   driver_set_pixel(driver, i, r/3, g/3, b/3);  // trail
   driver_set_pixel(driver, i, 0, 0, 0);         // off
   // ...
   led_strip_refresh(driver->strip);
   ```

4. **`app_driver_light_init()` clear (~line 1038):**
   ```cpp
   led_strip_clear(s_light_driver.strip);
   ```

### Step 7: Handle `apply_max_brightness`

No changes needed - `apply_max_brightness()` scales RGB values *before* they reach `driver_set_pixel()`, so the white extraction will operate on already-scaled values. This is correct because scaling then extracting white is equivalent to extracting white then scaling all 4 channels.

---

## Verification Plan

1. **Build test:** `idf.py build` with `CONFIG_TLED_LED_SK6812=y` (set via `idf.py menuconfig`)
2. **Build test:** `idf.py build` with `CONFIG_TLED_LED_WS2812B=y` (regression)
3. **Hardware test on SK6812 RGBW strip:**
   - Solid red → all LEDs red (no repeating 3-color pattern)
   - Solid white → uses W channel (brighter/cleaner white than RGB mixing)
   - Solid blue → all LEDs blue
   - Rainbow effect → smooth rainbow, no byte misalignment
   - Brightness limiting → all channels scale correctly
4. **Regression test on WS2812B strip:** existing behavior unchanged

## Potential Issues

1. **Component conflict:** The built-in ESP-IDF `led_strip` component may conflict with the managed component. If so, you may need to exclude the built-in one. Check build output for duplicate symbol errors.

2. **RMT channel allocation:** The legacy code explicitly configures `RMT_CHANNEL_0`. The modern API handles channel allocation internally. Remove all manual RMT config.

3. **v2.x vs v3.x API:** If using `espressif/led_strip` v3.x, the field names change:
   - `led_pixel_format` → `color_component_format`
   - `LED_PIXEL_FORMAT_GRBW` → `LED_STRIP_COLOR_COMPONENT_FMT_GRBW`
   - `LED_PIXEL_FORMAT_GRB` → `LED_STRIP_COLOR_COMPONENT_FMT_GRB`

   Check which version gets downloaded and adjust accordingly.

4. **WS2811 support:** The modern component has `LED_MODEL_WS2811`. Add a case for `CHIPSET_WS2811` in the init:
   ```cpp
   led_model_t model;
   switch (config->chipset) {
       case CHIPSET_SK6812: model = LED_MODEL_SK6812; break;
       case CHIPSET_WS2811: model = LED_MODEL_WS2811; break;
       default:             model = LED_MODEL_WS2812; break;
   }
   ```
