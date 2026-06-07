# chipguy_cyd2432S028_display

A self-contained **display + touch** driver for the **CYD ESP32-2432S028**
("Cheap Yellow Display") — the common ESP32 dev board with a 2.8" 240×320 SPI
LCD and a resistive touch panel. It drives the **ILI9341** display and the
**XPT2046** touch controller so you don't have to think about either: the
hardware is fixed and fully handled, leaving you to write an **LVGL 9** app and
nothing else.

The two bundled examples are the heart of the library. Each is a complete,
working **starting point** — the display, touch, and LVGL are already wired up
and running before your code gets control. You pick the one that matches how you
want to build your UI, copy it, and start replacing its placeholder UI with your
own.

> This is for the **ILI9341** flavor of the board. There are CYD variants with
> different controllers (e.g. ST7789); those need a different driver.

---

## Installation

This library is distributed as a GitHub ZIP — there is no Arduino Library
Manager entry. To install:

1. On the GitHub project page, click **Code ▸ Download ZIP**.
2. Unzip it. GitHub names the folder after the repository plus a branch suffix
   (e.g. `cyd2432S028_display-main`); **rename it to
   `chipguy_cyd2432S028_display`** so the folder name matches the library.
3. Move that folder into your Arduino **libraries** directory:
   - macOS / Linux: `~/Documents/Arduino/libraries/`
   - Windows: `Documents\Arduino\libraries\`
4. Restart the Arduino IDE.

(Alternatively, in the IDE: **Sketch ▸ Include Library ▸ Add .ZIP Library…**
and select the downloaded ZIP — but if it lands as `…-main`, rename it as above.)

### Dependency: LVGL 9

The examples need **LVGL 9** (developed against 9.3). Install it via the Arduino
**Library Manager** (search "lvgl"). Each example sketch folder ships its own
`lv_conf.h`; LVGL reads its configuration from that file, so keep it next to the
`.ino`.

### Arduino IDE board settings

The current ESP32 Arduino core ships a board definition for this exact board.
In **Tools ▸ Board ▸ esp32**, select **"ESP32-2432S028R CYD"** — it configures
the board specifics for you, so there's nothing else to set up. The examples
fit comfortably in the default partition.

---

## The two examples — pick your starting point

Open them from **File ▸ Examples ▸ chipguy_cyd2432S028_display**. Both come up
running on the hardware immediately, so you can confirm your board works before
you change a line. They differ only in **how you author the UI**.

To turn either one into your own project, **save it under a new name**
(File ▸ Save As) so your work lives outside the library, then start editing as
described below. Each example folder is self-contained — it carries its own
`lv_setup.hpp` and `lv_conf.h` — so a copied sketch is fully standalone.

### LvglClaudeCodeStub — write the UI yourself (by hand or with an AI)

Use this when you want to build the interface **in code**: directly, or by
handing the sketch to an AI coding assistant such as Claude Code.

The UI lives in a tiny `ui.h` / `ui.cpp` pair in the sketch folder. All the
hardware/LVGL setup happens in the sketch before `ui_init()` is called, so
`ui_init()` is a blank canvas: it receives a live LVGL screen and you create
widgets on it. To start your app:

1. Open `ui.cpp` and look at `ui_init()`. The demo it ships with (a label that
   follows your finger, plus a color-cycling rectangle) is just there to prove
   the display and touch work — **delete it.**
2. In its place, build your own UI on `lv_screen_active()` using normal LVGL
   calls (`lv_label_create`, `lv_button_create`, event callbacks, …).
3. As your app grows, add more screens and helper functions, and declare the
   ones the sketch needs in `ui.h`.

Because everything except the UI is already wired up, this scaffold is ideal to
hand to **Claude Code**: point it at the sketch folder and describe the app you
want — it can focus entirely on `ui.cpp` and the screens you add, without
touching display/touch/LVGL plumbing.

**Reach for this one** when you want full control, a tiny footprint, or
AI-assisted/hand-written UI code.

### Lvgl93SquarelineLauncher — design the UI visually in SquareLine Studio

Use this when you'd rather **design the interface visually** in
[SquareLine Studio](https://squareline.io/) (a drag-and-drop LVGL UI editor) and
just run the result on the CYD.

The sketch's `src/` folder holds a small **placeholder** UI in the exact file
shape a SquareLine export produces (`ui.c`, `ui_Screen1.c`, `ui_events.c`,
`ui_helpers.c`, …). The sketch calls the `ui_init()` that SquareLine generates.
To start your app:

1. In SquareLine Studio, create a project sized to your orientation
   (**240×320** for portrait, 320×240 for landscape), **16-bit** color, with **no
   rotation or offset** — this library handles rotation, so keep the project
   unrotated.
2. Lay out your screens visually.
3. **Export ▸ UI files** (the "Arduino TFT_eSPI profile" works, among others).
4. **Delete the placeholder files** in the sketch's `src/` folder and drop your
   exported files in their place. Build and upload — `ui_init()` now runs your
   design.

Your own event-handler code goes in `ui_events.c` (the one file SquareLine keeps
across re-exports), so you can re-export the visuals freely without losing logic.

**Reach for this one** when you want to iterate on layout visually and write
little or no C for the UI itself.

---

## What the examples give you: the `lv_setup` API

Both examples wire the hardware to LVGL through one small header,
**`lv_setup.hpp`** (plus **`lv_conf.h`** for LVGL's build config). That header,
and the global objects it defines, is the entire surface you interact with — and
once `begin()` returns, you are just using plain LVGL.

```cpp
#include "lv_conf.h"
#include "lvgl.h"
#include "lv_setup.hpp"

void setup() {
    Serial.begin(115200);

    lv_setup.begin(0);          // initialize display + touch + LVGL (rotation 0)

    // ...build your UI on lv_screen_active() here (or call ui_init())...
}

void loop() {
    lv_timer_handler();         // let LVGL run
    delay(5);
}
```

What the header provides:

- **`lv_setup.begin(uint8_t rotation = 0)`** — call once in `setup()`. Brings up
  the ILI9341, the XPT2046 touch, and LVGL; creates the LVGL display and a
  pointer (touch) input device and connects them. After this returns, the active
  LVGL screen exists and you build your UI with ordinary LVGL calls.
- **`display`** — a global display object. Handy methods:
  `display.width()`, `display.height()` (for the current rotation), and
  `display.setBacklight(0..100)` to dim/brighten the backlight.
- **`lv_setup.touch()`** — the touch driver, for calibration (below). You won't
  normally call it otherwise; `lv_setup` already feeds touch into LVGL.

You don't render or flush anything yourself — LVGL does, through the callbacks
`lv_setup` installed. Your job is to create LVGL widgets and call
`lv_timer_handler()` in `loop()`.

### Portable across chipguy board libraries

Because every hardware detail lives in just two files — **`lv_setup.hpp`** and
**`lv_conf.h`** — your application code (`ui.cpp`, or the SquareLine `src/`
files) is hardware-independent. It only ever talks to LVGL.

So to move an app to a **different display board**, drop in the `lv_setup.hpp`
and `lv_conf.h` from that board's chipguy library, and the rest of your sketch
is unchanged. The same UI runs on different panels with no edits to your widget
code. (Different boards have different resolutions, so alignment-based layouts
travel best; fixed pixel coordinates may need adjusting.)

### Rotation

Pass `0`–`3` (or the equivalent degrees) to `lv_setup.begin()`. Display and
touch rotate together.

| rotation | degrees | orientation | resolution |
|---|---|---|---|
| 0 | 0 | portrait (native) | 240×320 |
| 1 | 90 | landscape | 320×240 |
| 2 | 180 | portrait flipped | 240×320 |
| 3 | 270 | landscape flipped | 320×240 |

So `0`/`2` are portrait and `1`/`3` are landscape, and `90`/`180`/`270` are
accepted as aliases for `1`/`2`/`3` (e.g. `lv_setup.begin(90)` is landscape).
For the SquareLine example, size your SquareLine project to match the
orientation you choose.

### Touch calibration (only if needed)

The defaults work on typical CYDs. If touches land slightly off, read the raw
ADC values at the screen corners and set your own calibration rectangle:

```cpp
uint16_t rx, ry;
if (lv_setup.touch().readRaw(rx, ry))                 // press a corner, read Serial
    Serial.printf("raw %u, %u\n", rx, ry);

lv_setup.touch().setCalibration(xMin, xMax, yMin, yMax);  // from your readings
```

### Tuning note

`lv_setup.hpp` defines `LV_SETUP_BUFFER_ROWS` (the height of LVGL's partial
render buffer). Larger is smoother but uses more RAM; the default is a good
balance for this board.

---

## License

MIT — see [LICENSE](LICENSE).

The ILI9341 initialization/gamma values are standard panel-datasheet register
settings (as also used by Adafruit_ILI9341, BSD-3). `lv_conf.h` is based on
LVGL's template (LVGL is MIT-licensed). If you replace the placeholder
SquareLine files with a real SquareLine Studio export, that generated code
carries its own provenance.
