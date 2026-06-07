/*
 * ILI9341 SPI display driver for the CYD ESP32-2432S028.
 * See chipguy_cyd2432S028_display.h for the wiring and usage overview.
 */

#include "chipguy_cyd2432S028_display.h"
#include <driver/gpio.h>
#include <esp_heap_caps.h>

// ── Board pin assignments (fixed on the ESP32-2432S028) ────────────────────
#define CYD_TFT_MOSI  13
#define CYD_TFT_SCLK  14   // MISO is not used (write-only blitting), so it's left off the bus.
#define CYD_TFT_CS    15
#define CYD_TFT_DC     2
#define CYD_TFT_RST   12
#define CYD_TFT_BL    21   // backlight, active HIGH

// The display shares no bus with anything else on this board, so we clock it
// fast.  ILI9341 tolerates 40 MHz comfortably over the short CYD traces.
#define CYD_TFT_SPI_HZ  40000000
#define CYD_TFT_SPI_HOST  SPI2_HOST   // HSPI

// The ILI9341 on the CYD uses BGR color order; this is the base MADCTL with
// only the BGR bit set.  Rotation bits (MY/MX/MV) are OR'd in per rotation.
#define CYD_MADCTL_BGR  0x08
#define ILI9341_MADCTL_MY  0x80
#define ILI9341_MADCTL_MX  0x40
#define ILI9341_MADCTL_MV  0x20

// SPI DMA transfers are capped well under the ESP-IDF limit per chunk.
static const size_t CYD_SPI_MAX_CHUNK = 32768;

chipguy_cyd2432S028_display::chipguy_cyd2432S028_display() {}

// ── Low-level SPI helpers ──────────────────────────────────────────────────

void chipguy_cyd2432S028_display::_cmd(uint8_t cmd) {
    if (!_spi) return;
    gpio_set_level((gpio_num_t)CYD_TFT_DC, 0);   // command
    gpio_set_level((gpio_num_t)CYD_TFT_CS, 0);
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_data[0] = cmd;
    t.flags = SPI_TRANS_USE_TXDATA;
    spi_device_polling_transmit(_spi, &t);
    gpio_set_level((gpio_num_t)CYD_TFT_CS, 1);
}

void chipguy_cyd2432S028_display::_data(const uint8_t *data, size_t len) {
    if (!_spi || len == 0) return;
    gpio_set_level((gpio_num_t)CYD_TFT_DC, 1);   // data
    gpio_set_level((gpio_num_t)CYD_TFT_CS, 0);
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(_spi, &t);
    gpio_set_level((gpio_num_t)CYD_TFT_CS, 1);
}

void chipguy_cyd2432S028_display::_data8(uint8_t val) {
    _data(&val, 1);
}

// ── ILI9341 power-on init sequence ─────────────────────────────────────────
// Standard ILI9341 startup (gamma, power, frame rate).  MADCTL is written
// here as a placeholder and then re-issued by setRotation().

void chipguy_cyd2432S028_display::_initPanel() {
    _cmd(0xEF); { const uint8_t d[] = {0x03, 0x80, 0x02}; _data(d, 3); }
    _cmd(0xCF); { const uint8_t d[] = {0x00, 0xC1, 0x30}; _data(d, 3); }
    _cmd(0xED); { const uint8_t d[] = {0x64, 0x03, 0x12, 0x81}; _data(d, 4); }
    _cmd(0xE8); { const uint8_t d[] = {0x85, 0x00, 0x78}; _data(d, 3); }
    _cmd(0xCB); { const uint8_t d[] = {0x39, 0x2C, 0x00, 0x34, 0x02}; _data(d, 5); }
    _cmd(0xF7); _data8(0x20);
    _cmd(0xEA); { const uint8_t d[] = {0x00, 0x00}; _data(d, 2); }
    _cmd(0xC0); _data8(0x23);              // Power control 1
    _cmd(0xC1); _data8(0x10);              // Power control 2
    _cmd(0xC5); { const uint8_t d[] = {0x3E, 0x28}; _data(d, 2); }  // VCOM 1
    _cmd(0xC7); _data8(0x86);              // VCOM 2
    _cmd(0x36); _data8(CYD_MADCTL_BGR);    // MADCTL (overwritten by setRotation)
    _cmd(0x3A); _data8(0x55);              // Pixel format: 16-bit RGB565
    _cmd(0xB1); { const uint8_t d[] = {0x00, 0x18}; _data(d, 2); }  // Frame rate
    _cmd(0xB6); { const uint8_t d[] = {0x08, 0x82, 0x27}; _data(d, 3); } // Display func
    _cmd(0xF2); _data8(0x00);              // 3Gamma disable
    _cmd(0x26); _data8(0x01);              // Gamma curve selected
    _cmd(0xE0); { const uint8_t d[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E,
                  0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}; _data(d, 15); }
    _cmd(0xE1); { const uint8_t d[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31,
                  0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}; _data(d, 15); }
    _cmd(0x11);                            // Sleep out
    delay(120);
    _cmd(0x29);                            // Display on
    delay(20);
}

void chipguy_cyd2432S028_display::_setAddrWindow(int x0, int y0, int x1, int y1) {
    uint8_t col[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    _cmd(0x2A);              // CASET — Column Address Set
    _data(col, 4);

    uint8_t row[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
    };
    _cmd(0x2B);              // RASET — Row Address Set
    _data(row, 4);

    _cmd(0x2C);              // RAMWR — Memory Write
}

void chipguy_cyd2432S028_display::_pushPixels(const uint8_t *data, size_t bytes) {
    if (!_spi || bytes == 0) return;
    gpio_set_level((gpio_num_t)CYD_TFT_DC, 1);
    gpio_set_level((gpio_num_t)CYD_TFT_CS, 0);

    while (bytes > 0) {
        size_t chunk = (bytes > CYD_SPI_MAX_CHUNK) ? CYD_SPI_MAX_CHUNK : bytes;
        spi_transaction_t t = {};
        t.length = chunk * 8;
        t.tx_buffer = data;
        spi_device_polling_transmit(_spi, &t);
        data  += chunk;
        bytes -= chunk;
    }

    gpio_set_level((gpio_num_t)CYD_TFT_CS, 1);
}

// ── Public API ─────────────────────────────────────────────────────────────

bool chipguy_cyd2432S028_display::begin(uint16_t rotation) {
    // DC and CS are GPIO-driven (we manage them around each transaction).
    gpio_set_direction((gpio_num_t)CYD_TFT_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)CYD_TFT_CS, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)CYD_TFT_CS, 1);   // deselect

    // Hardware reset pulse.
    gpio_set_direction((gpio_num_t)CYD_TFT_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)CYD_TFT_RST, 1);
    delay(10);
    gpio_set_level((gpio_num_t)CYD_TFT_RST, 0);
    delay(20);
    gpio_set_level((gpio_num_t)CYD_TFT_RST, 1);
    delay(150);

    // Register the SPI bus with the ESP-IDF master driver.
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = CYD_TFT_MOSI;
    buscfg.miso_io_num = -1;            // MISO unused for write-only blitting
    buscfg.sclk_io_num = CYD_TFT_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = CYD_SPI_MAX_CHUNK;

    esp_err_t err = spi_bus_initialize(CYD_TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("cyd display: spi_bus_initialize failed: 0x%x\n", err);
        return false;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = CYD_TFT_SPI_HZ;
    devcfg.mode = 0;
    devcfg.spics_io_num = -1;           // manual CS
    devcfg.queue_size = 1;
    devcfg.flags = SPI_DEVICE_NO_DUMMY;

    err = spi_bus_add_device(CYD_TFT_SPI_HOST, &devcfg, &_spi);
    if (err != ESP_OK) {
        Serial.printf("cyd display: spi_bus_add_device failed: 0x%x\n", err);
        _spi = nullptr;
        return false;
    }

    _initPanel();
    setRotation(rotation);

    // Backlight on at full brightness.
    setBacklight(100);

    return true;
}

void chipguy_cyd2432S028_display::setRotation(uint16_t rotation) {
    _rotation = cyd_normalize_rotation(rotation);
    uint8_t madctl = CYD_MADCTL_BGR;

    switch (_rotation) {
        case 0:  // portrait, native
            madctl |= ILI9341_MADCTL_MX;
            _w = CYD_TFT_NATIVE_WIDTH;
            _h = CYD_TFT_NATIVE_HEIGHT;
            break;
        case 1:  // landscape
            madctl |= ILI9341_MADCTL_MV;
            _w = CYD_TFT_NATIVE_HEIGHT;
            _h = CYD_TFT_NATIVE_WIDTH;
            break;
        case 2:  // portrait flipped
            madctl |= ILI9341_MADCTL_MY;
            _w = CYD_TFT_NATIVE_WIDTH;
            _h = CYD_TFT_NATIVE_HEIGHT;
            break;
        case 3:  // landscape flipped
            madctl |= ILI9341_MADCTL_MV | ILI9341_MADCTL_MX | ILI9341_MADCTL_MY;
            _w = CYD_TFT_NATIVE_HEIGHT;
            _h = CYD_TFT_NATIVE_WIDTH;
            break;
    }

    _cmd(0x36);          // MADCTL
    _data8(madctl);
}

void chipguy_cyd2432S028_display::setBacklight(int percentage) {
    if (percentage < 0)   percentage = 0;
    if (percentage > 100) percentage = 100;
    // Active HIGH: higher duty = brighter.
    analogWrite(CYD_TFT_BL, percentage * 255 / 100);
}

void chipguy_cyd2432S028_display::flushRegion(int x1, int y1, int x2, int y2,
                                              uint16_t *pixels) {
    if (!_spi) return;

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;
    size_t count = (size_t)w * h;

    // LVGL renders RGB565 little-endian (native ESP32); the ILI9341 expects
    // big-endian (MSB first).  Swap in place before transmitting.
    for (size_t i = 0; i < count; i++) {
        pixels[i] = __builtin_bswap16(pixels[i]);
    }

    _setAddrWindow(x1, y1, x2, y2);
    _pushPixels((const uint8_t *)pixels, count * 2);
}

void chipguy_cyd2432S028_display::fillScreen(uint16_t color) {
    if (!_spi) return;
    uint16_t be = __builtin_bswap16(color);

    // One-row buffer pushed for every row.  Must be DMA-capable: the ESP-IDF
    // SPI driver requires DMA memory for transfers larger than the FIFO.
    uint16_t *line = (uint16_t *)heap_caps_malloc((size_t)_w * 2, MALLOC_CAP_DMA);
    if (!line) return;
    for (int i = 0; i < _w; i++) line[i] = be;

    _setAddrWindow(0, 0, _w - 1, _h - 1);
    for (int y = 0; y < _h; y++) {
        _pushPixels((const uint8_t *)line, (size_t)_w * 2);
    }
    heap_caps_free(line);
}
