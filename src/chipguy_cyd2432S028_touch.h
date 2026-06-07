/*
 * XPT2046 resistive touch driver for the CYD ESP32-2432S028.
 *
 * The XPT2046 on this board sits on its OWN set of SPI pins (separate from the
 * ILI9341 display bus), so this driver bit-bangs it directly — no bus sharing,
 * no extra library, and it stays well under the XPT2046's ~2.5 MHz limit.
 *
 * Board wiring (fixed on the ESP32-2432S028):
 *   XPT2046_CLK 25   XPT2046_MOSI 32   XPT2046_MISO 39
 *   XPT2046_CS  33   XPT2046_IRQ  36 (active LOW while touched)
 *
 * Resistive panels vary unit-to-unit, so raw ADC readings are mapped to screen
 * pixels through a calibration rectangle.  The defaults work on typical CYDs;
 * call setCalibration() to tune, or readRaw() to gather values for your panel.
 */

#pragma once

#include <Arduino.h>

class chipguy_cyd2432S028_touch {
public:
    chipguy_cyd2432S028_touch();

    // Configure the touch GPIOs.  Safe to call before the display is up.
    void begin();

    // Match the touch coordinate mapping to the display rotation, using the
    // same convention as chipguy_cyd2432S028_display: an index 0..3 or degrees
    // 0/90/180/270.
    void setRotation(uint16_t rotation);

    // Override the raw-ADC calibration rectangle.  xMin/xMax map across the
    // panel's physical X axis (240 px), yMin/yMax across the physical Y axis
    // (320 px), both before rotation is applied.
    void setCalibration(uint16_t xMin, uint16_t xMax, uint16_t yMin, uint16_t yMax);

    // Read the touch point.  Returns true if the screen is currently pressed,
    // with x,y mapped to screen pixels for the active rotation.
    bool read(uint16_t &x, uint16_t &y);

    // Read averaged raw 12-bit ADC values (no calibration/rotation applied).
    // Returns true if pressed.  Useful for gathering calibration numbers.
    bool readRaw(uint16_t &rawX, uint16_t &rawY);

private:
    uint8_t  _rotation = 0;
    uint16_t _xMin, _xMax, _yMin, _yMax;

    uint16_t _transferByteAndRead12(uint8_t cmd);
    bool     _readRawSample(uint16_t &rawX, uint16_t &rawY);
};
