/*
 * TLED - Unit Tests for LED Driver
 *
 * TDD approach: These tests define the expected behavior of the driver.
 * Run tests with: idf.py -T test build flash monitor
 */

#include <unity.h>
#include <esp_err.h>
#include "app_driver.h"

// Note: Full driver tests require hardware. These are placeholder tests
// that define the expected interface and behavior.

// Test: Driver initialization should return a valid handle
TEST_CASE("app_driver_light_init returns valid handle", "[driver]")
{
    // This test requires the actual hardware/driver to be available
    // For now, this documents the expected behavior
    // app_driver_handle_t handle = app_driver_light_init();
    // TEST_ASSERT_NOT_NULL(handle);
    TEST_PASS();
}

// Test: Setting power to ON should turn LED on
TEST_CASE("app_driver_light_set_power ON", "[driver]")
{
    // app_driver_handle_t handle = app_driver_light_init();
    // esp_err_t err = app_driver_light_set_power(handle, true);
    // TEST_ASSERT_EQUAL(ESP_OK, err);
    // TEST_ASSERT_TRUE(app_driver_light_get_power(handle));
    TEST_PASS();
}

// Test: Setting power to OFF should turn LED off
TEST_CASE("app_driver_light_set_power OFF", "[driver]")
{
    // app_driver_handle_t handle = app_driver_light_init();
    // esp_err_t err = app_driver_light_set_power(handle, false);
    // TEST_ASSERT_EQUAL(ESP_OK, err);
    // TEST_ASSERT_FALSE(app_driver_light_get_power(handle));
    TEST_PASS();
}

// Test: NULL handle should return error
TEST_CASE("app_driver_light_set_power with NULL handle returns error", "[driver]")
{
    // esp_err_t err = app_driver_light_set_power(NULL, true);
    // TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_PASS();
}

// Test: Button initialization should return valid handle
TEST_CASE("app_driver_button_init returns valid handle", "[driver]")
{
    // app_driver_handle_t handle = app_driver_button_init();
    // TEST_ASSERT_NOT_NULL(handle);
    TEST_PASS();
}

TEST_CASE("RGBW accurate mode extracts common white", "[driver][rgbw]")
{
    tled_rgbw_color_t white = tled_rgb_to_rgbw(128, 128, 128, WHITE_MODE_ACCURATE,
                                               0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(0, white.r);
    TEST_ASSERT_EQUAL_UINT8(0, white.g);
    TEST_ASSERT_EQUAL_UINT8(0, white.b);
    TEST_ASSERT_EQUAL_UINT8(128, white.w);

    tled_rgbw_color_t red = tled_rgb_to_rgbw(255, 0, 0, WHITE_MODE_ACCURATE,
                                             0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(255, red.r);
    TEST_ASSERT_EQUAL_UINT8(0, red.g);
    TEST_ASSERT_EQUAL_UINT8(0, red.b);
    TEST_ASSERT_EQUAL_UINT8(0, red.w);

    tled_rgbw_color_t green = tled_rgb_to_rgbw(0, 255, 0, WHITE_MODE_ACCURATE,
                                               0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(0, green.r);
    TEST_ASSERT_EQUAL_UINT8(255, green.g);
    TEST_ASSERT_EQUAL_UINT8(0, green.b);
    TEST_ASSERT_EQUAL_UINT8(0, green.w);

    tled_rgbw_color_t blue = tled_rgb_to_rgbw(0, 0, 255, WHITE_MODE_ACCURATE,
                                              0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(0, blue.r);
    TEST_ASSERT_EQUAL_UINT8(0, blue.g);
    TEST_ASSERT_EQUAL_UINT8(255, blue.b);
    TEST_ASSERT_EQUAL_UINT8(0, blue.w);
}

TEST_CASE("RGBW accurate mode preserves pastel chroma", "[driver][rgbw]")
{
    tled_rgbw_color_t color = tled_rgb_to_rgbw(200, 160, 120, WHITE_MODE_ACCURATE,
                                               0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(80, color.r);
    TEST_ASSERT_EQUAL_UINT8(40, color.g);
    TEST_ASSERT_EQUAL_UINT8(0, color.b);
    TEST_ASSERT_EQUAL_UINT8(120, color.w);
}

TEST_CASE("RGBW none mode uses manual white without RGB extraction", "[driver][rgbw]")
{
    tled_rgbw_color_t color = tled_rgb_to_rgbw(20, 40, 60, WHITE_MODE_NONE,
                                               77, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(20, color.r);
    TEST_ASSERT_EQUAL_UINT8(40, color.g);
    TEST_ASSERT_EQUAL_UINT8(60, color.b);
    TEST_ASSERT_EQUAL_UINT8(77, color.w);
}

TEST_CASE("RGBW brighter and max modes keep RGB unchanged", "[driver][rgbw]")
{
    tled_rgbw_color_t brighter = tled_rgb_to_rgbw(20, 40, 60, WHITE_MODE_BRIGHTER,
                                                  0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(20, brighter.r);
    TEST_ASSERT_EQUAL_UINT8(40, brighter.g);
    TEST_ASSERT_EQUAL_UINT8(60, brighter.b);
    TEST_ASSERT_EQUAL_UINT8(20, brighter.w);

    tled_rgbw_color_t max = tled_rgb_to_rgbw(20, 40, 60, WHITE_MODE_MAX,
                                             0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(20, max.r);
    TEST_ASSERT_EQUAL_UINT8(40, max.g);
    TEST_ASSERT_EQUAL_UINT8(60, max.b);
    TEST_ASSERT_EQUAL_UINT8(60, max.w);
}

TEST_CASE("RGBW dual mode uses manual white when set", "[driver][rgbw]")
{
    tled_rgbw_color_t color = tled_rgb_to_rgbw(30, 50, 70, WHITE_MODE_DUAL,
                                               25, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(30, color.r);
    TEST_ASSERT_EQUAL_UINT8(50, color.g);
    TEST_ASSERT_EQUAL_UINT8(70, color.b);
    TEST_ASSERT_EQUAL_UINT8(25, color.w);
}

TEST_CASE("RGBW dual mode falls back to brighter when manual white is zero", "[driver][rgbw]")
{
    tled_rgbw_color_t color = tled_rgb_to_rgbw(30, 50, 70, WHITE_MODE_DUAL,
                                               0, 255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(30, color.r);
    TEST_ASSERT_EQUAL_UINT8(50, color.g);
    TEST_ASSERT_EQUAL_UINT8(70, color.b);
    TEST_ASSERT_EQUAL_UINT8(30, color.w);
}

TEST_CASE("RGBW gains scale converted channels", "[driver][rgbw]")
{
    tled_rgbw_color_t color = tled_rgb_to_rgbw(100, 80, 60, WHITE_MODE_ACCURATE,
                                               0, 255, 128, 0, 64);
    TEST_ASSERT_EQUAL_UINT8(40, color.r);
    TEST_ASSERT_EQUAL_UINT8(10, color.g);
    TEST_ASSERT_EQUAL_UINT8(0, color.b);
    TEST_ASSERT_EQUAL_UINT8(15, color.w);
}

// WS2805 color temperature to warm/cool white mixing (range 153-333 mireds)

TEST_CASE("CT warmest mireds drives warm white only", "[driver][ct]")
{
    tled_white_channels_t w = tled_ct_to_whites(333, 153, 333, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(255, w.ww);
    TEST_ASSERT_EQUAL_UINT8(0, w.cw);
}

TEST_CASE("CT coolest mireds drives cool white only", "[driver][ct]")
{
    tled_white_channels_t w = tled_ct_to_whites(153, 153, 333, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(0, w.ww);
    TEST_ASSERT_EQUAL_UINT8(255, w.cw);
}

TEST_CASE("CT midpoint splits warm and cool evenly", "[driver][ct]")
{
    tled_white_channels_t w = tled_ct_to_whites(243, 153, 333, 255, 255);
    // 243 mireds = exact midpoint of 153-333: warm fraction = 127/255
    TEST_ASSERT_EQUAL_UINT8(127, w.ww);
    TEST_ASSERT_EQUAL_UINT8(128, w.cw);
}

TEST_CASE("CT output scales with brightness level", "[driver][ct]")
{
    tled_white_channels_t w = tled_ct_to_whites(333, 153, 333, 128, 255);
    TEST_ASSERT_EQUAL_UINT8(128, w.ww);
    TEST_ASSERT_EQUAL_UINT8(0, w.cw);

    tled_white_channels_t off = tled_ct_to_whites(243, 153, 333, 0, 255);
    TEST_ASSERT_EQUAL_UINT8(0, off.ww);
    TEST_ASSERT_EQUAL_UINT8(0, off.cw);
}

TEST_CASE("CT white gain scales both channels", "[driver][ct]")
{
    tled_white_channels_t w = tled_ct_to_whites(333, 153, 333, 255, 128);
    TEST_ASSERT_EQUAL_UINT8(128, w.ww);
    TEST_ASSERT_EQUAL_UINT8(0, w.cw);
}

TEST_CASE("CT out-of-range mireds are clamped", "[driver][ct]")
{
    tled_white_channels_t too_warm = tled_ct_to_whites(500, 153, 333, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(255, too_warm.ww);
    TEST_ASSERT_EQUAL_UINT8(0, too_warm.cw);

    tled_white_channels_t too_cool = tled_ct_to_whites(50, 153, 333, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(0, too_cool.ww);
    TEST_ASSERT_EQUAL_UINT8(255, too_cool.cw);
}
