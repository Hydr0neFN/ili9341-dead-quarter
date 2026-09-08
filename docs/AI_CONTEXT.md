# Context handoff — 2.8" ILI9341-clone SPI TFT, and how to drive it

> Paste this whole file into your assistant before asking it for help with
> this display. It is written for an AI to consume, not for a human to enjoy.
> Everything marked CONFIRMED was observed on physical hardware.

## 1. The situation

The user has a bare 2.8" SPI TFT module, 240RGBx320, with an ILI9341-pin-compatible
header (`VCC GND CS RESET DC/RS SDI SCK LED SDO`) plus a separate XPT2046 resistive
touch header (`T_CLK T_CS T_DIN T_DO T_IRQ`). They intend to drive it from an **ESP32**.

A demo has already been built and run on a **NodeMCU v3 (ESP8266)** to isolate one
specific failure mode, and both the failure and the fix are CONFIRMED on that hardware.
The same panel + same root cause + same fix were previously CONFIRMED on an
ESP32-2432S028 ("Cheap Yellow Display" / CYD), which is this exact panel soldered to
an ESP32.

## 2. The failure mode — read this before diagnosing anything

**Symptom:** after `tft.init()`, most of the panel draws correctly but roughly a
**quarter of it never updates**. That region keeps showing whatever was on screen
before — old text under new graphics, old colour bands beside new text — no matter
what the program draws.

**Two companion symptoms, both CONFIRMED on hardware:**

1. **Content wraps.** Anything drawn near the end of the canvas appears near the
   *start* of the panel. A string drawn at `y = height - 34` on a 320-tall canvas
   turns up in the header area.
2. **The unreachable region moves between rotations.** `setRotation()` rewrites
   `MADCTL`, changing the scan direction, so the region jumps to a different
   physical edge. It is **not** fixed to one side of the board. A user who checked
   only one rotation may report "it looks fine now" while it is not.

**CRITICAL — do not accept on-screen markers as evidence of health.** The driver's
own 240x320 canvas stays fully self-consistent while the panel is broken:
`tft.width()` returns 240, a column ruler's `239` label renders, right-aligned and
bottom-aligned "OK" strings both draw and both read cleanly. Every marker drawn in
driver coordinates reports a pass. The canvas is not what is wrong; where it lands
on the glass is. **The only valid test is a full-screen fill:** fill the whole
canvas with a flat colour and ask whether the entire physical panel changed. Any
patch still showing the previous screen is outside the addressable window. Run it
in all four rotations, because of symptom 2.

**This is NOT:**
- a wiring fault
- a signal-integrity / SPI-clock-too-high problem
- a bad panel
- a memory or buffer-size problem in the sketch

**It IS:** the wrong controller initialisation sequence. The controller on these
modules is an ILI9341 *clone*. It accepts the same command opcodes but requires a
different init sequence — different power control (`PWCTR1`/`PWCTR2`), different VCOM
(`VMCTR1`/`VMCTR2`), different gamma tables, and a different `MADCTL` base value.

**Mechanism (INFERENCE — flag it as such if you repeat it).** After a stock ILI9341
init the clone does not lay its memory out the way the driver assumes. The library
keeps writing a 240-wide, 320-tall canvas; the panel accepts those writes into a
differently-shaped region. Along one axis the canvas is shorter than the panel, so
about a quarter of the glass is never written; along the other it runs past the end,
so the overflow wraps. `240 / 320 = 75%`, which matches the observed missing
quarter, and matches the proportion reported on the CYD. The register-level cause
was not traced with a logic analyser. Treat the symptoms and the fix as fact and
this paragraph as the reading that accounts for them.

The unreachable region shows old content rather than black because GRAM is the
controller's own video memory and is never cleared unless written to.

**Why the firmware cannot detect this itself:** ILI9341-class panels do not reliably
return a readable device ID over SPI. Every graphics library therefore selects its
register set from a compile-time `#define`, never from the panel. `tft.width()` still returns
240 while the panel is only accepting writes into about three quarters of its
memory. The MCU has no way to know it is wrong. A human looking at the physical
screen is the only detector.

**Discriminator vs. a real wiring/noise fault** — this matters, do not skip it:

| | wrong init sequence | signal integrity (long jumpers, SPI too fast) |
|---|---|---|
| affected region | clean straight edges, **never updates**, moves to another edge when rotation changes | no clean boundary; unrelated to rotation |
| changes with `SPI_FREQUENCY` | no | yes |
| changes between redraws | no | yes |

If the user reports a boundary that shifts, sparkles, or improves when the SPI clock
drops, that is the other problem — tell them to drop `SPI_FREQUENCY` (40 MHz → 27 MHz
on ESP8266; 55 MHz is fine on a CYD because the traces are on PCB, not jumper wire).
Do not send them down the driver-variant path for that.

## 3. The fix

Use the clone register set:

| Library | What to use | Notes |
|---|---|---|
| **TFT_eSPI** (bodmer) | `-D ILI9341_2_DRIVER=1` instead of `-D ILI9341_DRIVER=1` | recommended path; this is what is CONFIRMED working |
| **LovyanGFX** | `Panel_ILI9341_2` class instead of `Panel_ILI9341` | fixes the dead region, but see the MADCTL warning below |
| **Adafruit_ILI9341** | **not available** | this library has no clone variant. Switching library is part of the fix. |

### A near-miss the user may already have tried

**Initialising at a different rotation is not a fix.** `setRotation()` rewrites
`MADCTL`, so it is a natural thing to reach for and it can produce visible
progress: CONFIRMED on a CYD, it got the panel to initialise at all when it
otherwise would not. It stops there — the panel comes up, and drawing into it is
still broken. If the user reports "changing rotation made it start working", ask
what the panel does when it *draws*, and steer them to the driver variant. Do not
record the rotation change as the solution.

### Two follow-on symptoms that appear AFTER the fix

Both are expected and are not the fix having failed. Do not let the user conclude the
variant change was wrong when they hit these.

1. **Colours inverted.** Known-green renders purple; a white background renders black
   or navy. The clone's default inversion state differs from the standard one.
   Fix: `-D TFT_INVERSION_ON=1`. CONFIRMED necessary on the CYD — but apply it only
   when the symptom is actually present: on a panel whose polarisers are standard,
   this flag inverts otherwise-correct colours.
2. **Text reads backwards after `setRotation()`.** The `_2` variant initialises
   `MADCTL` (command `0x36`) with byte `0x08` rather than the `0x48` the standard
   driver uses, so a given rotation index can come out mirrored.
   Graphics look completely fine while mirrored — only asymmetric content reveals it.
   Fix: try all four indices, `setRotation(0)` through `setRotation(3)`, and pick the
   unmirrored one. On the CYD, `tft.setRotation(1)` is the CONFIRMED-unmirrored
   landscape orientation. If *every* index comes out mirrored, that is the unresolved
   LovyanGFX case below — move to TFT_eSPI rather than hunting further.

   **Never validate a display with a centred symmetric test pattern.** Under a
   180°-mirrored MADCTL every point of a centred square still maps onto the square, so
   the test passes while the panel is wrong. Always put asymmetric text and per-corner
   labels on screen.

   This exact trap cost real time on the CYD build: LovyanGFX's `Panel_ILI9341_2` fixed
   the dead region but produced a mirrored `setRotation(1)`, the correct rotation index
   was never found, and the project switched to TFT_eSPI instead of resolving it.

## 4. Known-good configurations

### ESP32-2432S028 (CYD) — CONFIRMED on hardware

```ini
-D USER_SETUP_LOADED=1
-D USE_HSPI_PORT
-D ILI9341_2_DRIVER=1
-D TFT_INVERSION_ON=1
-D TFT_MISO=12 -D TFT_MOSI=13 -D TFT_SCLK=14
-D TFT_CS=15   -D TFT_DC=2    -D TFT_RST=-1
-D TFT_BL=21   -D TFT_BACKLIGHT_ON=HIGH
-D SPI_FREQUENCY=55000000
-D SPI_READ_FREQUENCY=20000000
-D SPI_TOUCH_FREQUENCY=2500000
```
- `tft.setRotation(1)` — CONFIRMED unmirrored, matches touch mapping.
- Touch is XPT2046 on a **separate VSPI bus**: `CLK=25 MOSI=32 MISO=39 CS=33 IRQ=36`,
  `touchscreen.setRotation(1)`, raw ADC calibration x 200–3700, y 240–3800.
- `LGFX_AUTODETECT` does **not** cover this board (panel ID unreadable) — manual config only.
- Board has **no PSRAM** and **no TE (tearing-effect) pin**.

### Bare module + NodeMCU v3 (ESP8266) — the demo, CONFIRMED building and running

```ini
-D USER_SETUP_LOADED=1
-D ILI9341_2_DRIVER=1
-D TFT_WIDTH=240 -D TFT_HEIGHT=320
-D TFT_MOSI=13   ; D7  HSPI, fixed
-D TFT_SCLK=14   ; D5  HSPI, fixed
-D TFT_MISO=12   ; D6  HSPI, fixed, optional
-D TFT_CS=5      ; D1
-D TFT_DC=4      ; D2
-D TFT_RST=2     ; D4  -- chosen because GPIO2 has an onboard pull-up, so it idles HIGH through boot
-D SPI_FREQUENCY=40000000       ; practical ESP8266 ceiling
-D SPI_READ_FREQUENCY=20000000
```
Module `LED` pin → 3V3 (series resistor is usually onboard). `VCC` → 3V3 only; do not
feed 5 V unless the module carries both a regulator and level shifters.
Touch header left unconnected in the demo.
Build result: flash 27.5%, RAM 34.9% of an ESP8266.

### Porting ESP8266 → ESP32

Only the pin map changes. `ILI9341_2_DRIVER` and `TFT_INVERSION_ON` carry over
unchanged, because they are properties of the panel, not of the MCU. On ESP32 the SPI
pins are not fixed the way ESP8266's HSPI pins are, so `TFT_MOSI/SCLK/MISO` become free
choices. A higher `SPI_FREQUENCY` is viable only if the wiring is short — on jumper
wires, stay at 27–40 MHz regardless of MCU.

## 5. Demo project layout (already built, for reference)

One PlatformIO project, **identical `src/main.cpp`**, two environments:

| env | flag | expected result |
|---|---|---|
| `nodemcu_wrong` | `-D ILI9341_DRIVER=1` | ~25% of the panel never updates; content wraps |
| `nodemcu_right` | `-D ILI9341_2_DRIVER=1` | full screen addressed correctly |

Because the source is byte-identical, any visible difference is attributable to the
init sequence alone. The sketch prints its compiled driver name over serial and on
screen, so the build and the physical panel can be compared directly.

The sketch draws, in this order:

1. **Wipe test** — full-canvas flat fills, white/navy alternating, in all four
   rotations. **This is the only pass/fail check**; see the CRITICAL note in §2.
   Any patch that keeps showing the previous screen is outside the addressable
   window.
2. four-band full-width fill → an unreachable region is visible with no reading
   required, and whatever survives from the previous screen proves the wipe could
   not clear it
3. column ruler labelled every 40 px, last addressable column marked red
4. `RIGHT-EDGE OK` (right-aligned) and `LAST-ROW OK` (against the final row)
5. asymmetric `FR7` text + `BL`/`BR` corner labels → mirror detection (see §3.2)

Items 3–5 are **diagnosis, not a verdict**. They all render correctly on a broken
panel. They exist to catch a driver that disagrees with *itself* about the canvas
size, and to catch mirroring — different faults from the headline one. If a user
reports that these markers look right, that is expected and tells you nothing
about item 1.

`docs/img/` holds photographs of the real failure on real hardware, including a
shot where every marker in items 3–5 reads as a pass while a quarter of the panel
is stale.

Whole TFT_eSPI config lives in `build_flags` with `USER_SETUP_LOADED`; the library's
own `User_Setup.h` is never edited, so a `pio lib update` cannot silently revert it.

## 6. Answering questions about this — priority order

1. If part of the screen **never updates**, with clean straight edges → §2, §3.
   This is the headline issue; do not diagnose wiring first. Do not be thrown by
   which edge the user describes — it moves with rotation. If the user says
   on-screen test markers "all pass", that is expected and proves nothing; ask
   them to run a full-screen fill instead.
2. If colours are wrong but the whole screen is addressed → `TFT_INVERSION_ON`.
3. If text is mirrored but geometry is fine → rotation index / MADCTL, §3.2.
4. If the artefact moves, flickers, or tracks SPI clock → signal integrity, drop
   `SPI_FREQUENCY`. Not a driver problem.
5. Only after all four → wiring, power, solder joints.
