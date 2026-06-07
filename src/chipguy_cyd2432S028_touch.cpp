/*
 * XPT2046 resistive touch driver for the CYD ESP32-2432S028.
 * See chipguy_cyd2432S028_touch.h for wiring and usage.
 */

#include "chipguy_cyd2432S028_touch.h"
#include "chipguy_cyd2432S028_display.h"   // for CYD_TFT_NATIVE_WIDTH/HEIGHT

// ── Board pin assignments (fixed on the ESP32-2432S028) ────────────────────
#define CYD_TOUCH_CLK   25
#define CYD_TOUCH_MOSI  32
#define CYD_TOUCH_MISO  39
#define CYD_TOUCH_CS    33
#define CYD_TOUCH_IRQ   36   // active LOW while the panel is being touched

// XPT2046 control bytes: start bit + channel select, 12-bit, differential.
#define XPT2046_CMD_X   0xD0   // read X position
#define XPT2046_CMD_Y   0x90   // read Y position

// Default raw-ADC calibration rectangle (typical CYD; tune with readRaw()).
#define CYD_TOUCH_X_MIN  200
#define CYD_TOUCH_X_MAX  3900
#define CYD_TOUCH_Y_MIN  200
#define CYD_TOUCH_Y_MAX  3900

// Number of samples averaged per read to smooth resistive jitter.
static const int CYD_TOUCH_SAMPLES = 4;

chipguy_cyd2432S028_touch::chipguy_cyd2432S028_touch()
    : _xMin(CYD_TOUCH_X_MIN), _xMax(CYD_TOUCH_X_MAX),
      _yMin(CYD_TOUCH_Y_MIN), _yMax(CYD_TOUCH_Y_MAX) {}

void chipguy_cyd2432S028_touch::begin() {
    pinMode(CYD_TOUCH_CLK, OUTPUT);
    pinMode(CYD_TOUCH_MOSI, OUTPUT);
    pinMode(CYD_TOUCH_MISO, INPUT);
    pinMode(CYD_TOUCH_CS, OUTPUT);
    // IRQ is GPIO36, an input-only pin with NO internal pull resistor; the
    // XPT2046's PENIRQ drives it high when idle, low when touched.
    pinMode(CYD_TOUCH_IRQ, INPUT);

    digitalWrite(CYD_TOUCH_CS, HIGH);   // deselect
    digitalWrite(CYD_TOUCH_CLK, LOW);
}

void chipguy_cyd2432S028_touch::setRotation(uint16_t rotation) {
    _rotation = cyd_normalize_rotation(rotation);
}

void chipguy_cyd2432S028_touch::setCalibration(uint16_t xMin, uint16_t xMax,
                                               uint16_t yMin, uint16_t yMax) {
    _xMin = xMin; _xMax = xMax;
    _yMin = yMin; _yMax = yMax;
}

// Bit-bang one XPT2046 conversion: send the 8-bit command, then clock out the
// 12-bit result (MSB first).  SPI mode 0, sampled on the rising edge.
uint16_t chipguy_cyd2432S028_touch::_transferByteAndRead12(uint8_t cmd) {
    // Send command byte.
    for (int i = 7; i >= 0; i--) {
        digitalWrite(CYD_TOUCH_MOSI, (cmd >> i) & 1);
        digitalWrite(CYD_TOUCH_CLK, HIGH);
        digitalWrite(CYD_TOUCH_CLK, LOW);
    }

    // One busy clock between command and result.
    digitalWrite(CYD_TOUCH_CLK, HIGH);
    digitalWrite(CYD_TOUCH_CLK, LOW);

    // Clock in 12 result bits, MSB first.
    uint16_t value = 0;
    for (int i = 0; i < 12; i++) {
        digitalWrite(CYD_TOUCH_CLK, HIGH);
        value = (value << 1) | (digitalRead(CYD_TOUCH_MISO) & 1);
        digitalWrite(CYD_TOUCH_CLK, LOW);
    }
    return value;
}

bool chipguy_cyd2432S028_touch::_readRawSample(uint16_t &rawX, uint16_t &rawY) {
    // IRQ is pulled low by the controller only while the panel is pressed.
    if (digitalRead(CYD_TOUCH_IRQ) != LOW) return false;

    uint32_t sumX = 0, sumY = 0;
    digitalWrite(CYD_TOUCH_CS, LOW);
    for (int i = 0; i < CYD_TOUCH_SAMPLES; i++) {
        sumX += _transferByteAndRead12(XPT2046_CMD_X);
        sumY += _transferByteAndRead12(XPT2046_CMD_Y);
    }
    digitalWrite(CYD_TOUCH_CS, HIGH);

    // Still pressed after sampling?  Rejects spurious single-edge IRQs.
    if (digitalRead(CYD_TOUCH_IRQ) != LOW) return false;

    rawX = sumX / CYD_TOUCH_SAMPLES;
    rawY = sumY / CYD_TOUCH_SAMPLES;
    return true;
}

bool chipguy_cyd2432S028_touch::readRaw(uint16_t &rawX, uint16_t &rawY) {
    return _readRawSample(rawX, rawY);
}

bool chipguy_cyd2432S028_touch::read(uint16_t &x, uint16_t &y) {
    uint16_t rawX, rawY;
    if (!_readRawSample(rawX, rawY)) return false;

    // Clamp to the calibration rectangle, then map to physical pixels
    // (240 wide x 320 tall, before rotation).
    long px = map((long)constrain(rawX, _xMin, _xMax), _xMin, _xMax,
                  0, CYD_TFT_NATIVE_WIDTH - 1);
    long py = map((long)constrain(rawY, _yMin, _yMax), _yMin, _yMax,
                  0, CYD_TFT_NATIVE_HEIGHT - 1);

    // The XPT2046's raw X increases opposite the ILI9341's X axis on this
    // board, so mirror it.  (Done in physical space so every rotation inherits
    // the correction.)
    px = (CYD_TFT_NATIVE_WIDTH - 1) - px;

    const long W = CYD_TFT_NATIVE_WIDTH - 1;    // 239
    const long H = CYD_TFT_NATIVE_HEIGHT - 1;   // 319

    switch (_rotation) {
        case 0:  x = px;          y = py;          break;  // portrait
        case 1:  x = py;          y = W - px;      break;  // landscape
        case 2:  x = W - px;      y = H - py;      break;  // portrait flipped
        case 3:  x = H - py;      y = px;          break;  // landscape flipped
    }
    return true;
}
