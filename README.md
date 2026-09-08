# The Dead Quarter

**Why a quarter of your cheap 2.8" ILI9341 panel shows static — and the one build
flag that fixes it.**

繁體中文版：[README.zh-TW.md](README.zh-TW.md)

A minimal PlatformIO project for the NodeMCU v3 (ESP8266) that demonstrates this
failure and its fix **on one board, with one source file, in two builds**. If you
landed here from a search, [the explanation](#why-this-happens) is probably what
you want; the demo exists so you can prove it to yourself in about two minutes.

---

## The symptom

After `tft.init()`, roughly three quarters of the panel draws correctly. The
remaining **last quarter** shows static that never changes, no matter what your
program draws into it.

It is not a wiring fault, not a bad panel, not SPI running too fast, and not a
buffer-size problem in your sketch. It is the wrong controller init sequence.

### It looks like it moves, but it doesn't

The dead region is always the last quarter of the panel's memory, and physically
always the same edge: **the one opposite the pin header**. What changes is how it
*appears*, and that depends only on `setRotation()`:

| Orientation | How the same defect looks |
|---|---|
| portrait, `setRotation(0)` | a band across the bottom — in a four-band colour test, the fourth band never arrives |
| landscape, `setRotation(1)` | a strip down one side |

Both descriptions are out there in forum posts and issue threads, and they read
like two different bugs. They are the same one. If you searched for "right-hand
columns" and found nothing useful, try the other description.

---

## Try it yourself

Two environments, **identical `src/main.cpp`**. Only the driver variant differs,
so anything you see on the glass is caused by the init sequence and nothing else.

| Environment | Driver flag | Expected result |
|---|---|---|
| `nodemcu_wrong` | `ILI9341_DRIVER` | last ~25% of the panel never written — static noise |
| `nodemcu_right` | `ILI9341_2_DRIVER` | full panel addressed correctly |

```sh
pio run -e nodemcu_wrong -t upload && pio device monitor
pio run -e nodemcu_right -t upload && pio device monitor
```

The sketch reports its compiled driver over serial and on screen, so the build and
the physical panel can be compared directly.

---

## Why this happens

### The controller isn't really an ILI9341

Cheap 2.8" 240×320 modules — including the all-in-one boards built around this
same panel, such as the **ESP32-2432S028**, known in the hobby community as the
"CYD" (*Cheap Yellow Display*) — almost never carry a genuine Ilitek ILI9341
controller. They carry a clone.

The clone understands the same *commands*. Send it `0x2A` and it knows you mean
"set the column address window". That is why the display isn't simply dead: a lot
of it works.

What the clone does **not** share is the startup sequence. It needs different
internal power settings (registers `PWCTR1` and `PWCTR2`), different
liquid-crystal voltage levels (VCOM, via `VMCTR1` and `VMCTR2`), different
colour-curve corrections (the gamma tables), and it has a different default for
`MADCTL` — the register that defines orientation and memory scan direction.

Sending it the sequence a genuine ILI9341 expects leaves the clone misreading
those power and orientation commands, and it comes up with its address window —
the region of its own memory it will accept writes into — covering only about
three quarters of the panel. Anything written past that limit is discarded, with
no error of any kind.

### Why static, and not black

```
   your sketch calls fillScreen(RED)
              |
              v
   library writes 240 x 320 pixels of red
              |
              v
   controller accepts them -- but only up to the limit of
   its (wrongly configured) address window
              |
              +-----------------------------+
              |                             |
              v                             v
    the first three quarters,        the last quarter,
    inside the window                outside it
    -> become red                    -> receives nothing
                                            |
                                            v
                                     it still holds whatever
                                     was in GRAM from before
```

GRAM (Graphics RAM) is the video memory inside the display controller itself, not
in the microcontroller. It powers up holding undefined values and it is never
cleared unless something writes to it. The region your sketch could not reach is
showing you that undefined content.

That is the whole reason this bug is so confusing: it doesn't look like an error.
It looks like the display is showing a *different program's* output, or like the
panel is physically damaged, or like a data line has a bad solder joint. It is
none of those.

### Why the microcontroller can't just detect it

It cannot ask the panel. ILI9341-class displays do not reliably report their
identifier over SPI: some clones return nothing, some return random values, some
return a genuine ILI9341 ID despite having incompatible internals.

So every graphics library on every platform picks its register set from a
compile-time `#define` and simply trusts it. `tft.width()` still returns `240` on
the broken build — the firmware genuinely believes it is addressing the whole
panel.

**You are the detector.** There is no software check the MCU can run on its own;
the fault can only be verified by looking at the physical screen.

---

## Telling it apart from a wiring problem

Both can produce a mess on screen, and the fixes have nothing in common. Check
this *before* you start re-seating jumper wires.

| | Wrong init sequence | Bad wiring / SPI too fast |
|---|---|---|
| Where the corruption starts | a straight, clean boundary in **the same place every single boot**, on the edge opposite the pins | no clean boundary |
| Behaviour over time | perfectly stable | sparkles, flickers, changes |
| Effect of lowering `SPI_FREQUENCY` | none at all | improves or disappears |

A stable boundary in a fixed place is the signature of this bug. If dropping the
SPI clock from 40 MHz to 27 MHz helps, you have the *other* problem — signal
integrity on long jumper wires, and no driver flag will fix it.

---

## The fix

```ini
-D ILI9341_2_DRIVER=1     ; instead of  -D ILI9341_DRIVER=1
```

TFT_eSPI includes the clone's register set as a dedicated driver. That is all it
takes.

On other libraries:

| Library | Clone variant |
|---|---|
| **TFT_eSPI** (bodmer) | `ILI9341_2_DRIVER` — this is the path confirmed working here |
| **LovyanGFX** | the `Panel_ILI9341_2` class (but read the MADCTL note below) |
| **Adafruit_ILI9341** | **none — there is no flag.** Migrating to TFT_eSPI or LovyanGFX is part of the fix. |

### Two side effects you might hit next

Neither means the flag was wrong. They are secondary effects of the same hardware
mismatch.

**1. Colours may be inverted.** Green comes out purple, white backgrounds come out
black or navy. The clone's default inversion state differs from the standard one.
**Only if you actually see this**, add:

```ini
-D TFT_INVERSION_ON=1
```

If your colours are already correct, leave it out — on a panel with standard
polarisers this flag would invert them.

**2. Text may read backwards after `setRotation()`.** The `_2` variant initialises
`MADCTL` (command `0x36`) with byte `0x08` instead of the `0x48` the standard
driver uses, so a given rotation index can come out mirrored. Try all four
indices, `setRotation(0)` through `setRotation(3)`, and use the one where text
reads left to right.

> **A centred, symmetric test pattern cannot detect a mirrored display.**
> Under a mirror, every point of a centred square still lands on the square. The
> test passes. The panel is wrong.

That is why this demo prints an asymmetric label (`FR7`) and marks the
bottom-left (`BL`) and bottom-right (`BR`) corners rather than a tidy centred
colour square. The trap is real: on a CYD, LovyanGFX's `Panel_ILI9341_2` fixed
the dead region but produced a mirrored `setRotation(1)`; the correct rotation
index was never found and that project moved to TFT_eSPI instead.

---

## Wiring — NodeMCU v3 → module

`MOSI`, `SCK` and `MISO` are the ESP8266's fixed HSPI pins and cannot be moved.
`CS`, `DC` and `RST` are free choices; these three were picked because none of
them fights the ESP8266's boot strapping.

| Module pin | NodeMCU | GPIO | Note |
|---|---|---|---|
| `VCC` | `3V3` | — | 3.3 V only. Do **not** feed 5 V unless the module has a regulator *and* level shifters. |
| `GND` | `GND` | — | |
| `CS` | `D1` | 5 | |
| `RESET` | `D4` | 2 | GPIO2 has an onboard pull-up, so it idles HIGH through boot. |
| `DC` / `RS` | `D2` | 4 | |
| `SDI` / `MOSI` | `D7` | 13 | HSPI, fixed |
| `SCK` | `D5` | 14 | HSPI, fixed |
| `LED` | `3V3` | — | The series resistor is usually already on the module. Wire to a GPIO instead if you want PWM dimming. |
| `SDO` / `MISO` | `D6` | 12 | HSPI, fixed. Optional — only needed for register readback. |

The touch header (`T_CLK` / `T_CS` / `T_DIN` / `T_DO` / `T_IRQ`) is left
unconnected. This demo is display-only on purpose: touch is a separate XPT2046
controller and adds nothing to the question of whether the panel initialises.

If you see sparkle or intermittent noise, drop `SPI_FREQUENCY` from `40000000`
to `27000000`. That is signal integrity on long jumper wires — a different
problem from the one above, and it moves and flickers where the init boundary
does not.

---

## What the demo draws

1. **Four-band fill** (red / green / blue / magenta, full width) — a missing or
   truncated band is obvious with no reading required. On a mis-initialised clone
   the **last** band is the one that goes.
2. **Edge frame + corner ticks** — the ticks on the far edge disappear when the
   panel is only partially addressed.
3. **Column ruler**, labelled every 40 px, with the last addressable column marked
   in red. If the panel reports 240 wide but the `239` label isn't there, the
   panel and the driver disagree.
4. **Two pass/fail strings, one per axis** — `RIGHT-EDGE OK` against the side
   edge, `LAST-ROW OK` against the final row, plus a solid bar hard against the
   last line. Both must be fully readable.

   A single-axis marker is not enough: the dead region lands on a different axis
   depending on rotation, so one marker passes the test in half the rotations
   while the panel is still broken.
5. **`FR7` + `BL` / `BR` corner labels** — mirror detection.
6. It cycles all four rotations, then repeats.

---

## Carrying this to an ESP32

Only the pin map changes. `ILI9341_2_DRIVER` and `TFT_INVERSION_ON` are
properties of *the panel*, not of the microcontroller, so they carry over
untouched.

Two practical differences:

- SPI pins can be mapped to almost any GPIO on an ESP32, unlike the ESP8266 which
  relies on its dedicated hardware SPI pins (the HSPI bus: `MOSI` on GPIO13 /
  silkscreen D7, `SCK` on GPIO14 / D5, `MISO` on GPIO12 / D6).
- You can raise `SPI_FREQUENCY`, but only over short electrical paths. A CYD
  handles 55 MHz because its connections are traces on the PCB; jumper-wire
  setups should stay between 27 and 40 MHz regardless of the MCU's rated speed.

### Known-good CYD configuration (ESP32-2432S028)

That board *is* this panel soldered to an ESP32, so everything above applies to
it unchanged.

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

- `tft.setRotation(1)` — confirmed unmirrored, and matches the touch mapping.
- Touch is XPT2046 on a **separate VSPI bus**: `CLK=25 MOSI=32 MISO=39 CS=33
  IRQ=36`, `touchscreen.setRotation(1)`, raw ADC calibration x 200–3700,
  y 240–3800.
- `LGFX_AUTODETECT` does **not** cover this board (the panel ID is unreadable) —
  manual configuration only.
- The board has **no PSRAM** and **no TE (tearing-effect) pin**.

---

## Notes

- The whole TFT_eSPI configuration lives in `build_flags` with
  `USER_SETUP_LOADED`, so the library's own `User_Setup.h` is never edited and a
  `pio pkg update` cannot silently revert it.
- [`docs/AI_CONTEXT.md`](docs/AI_CONTEXT.md) is the same knowledge written for an
  LLM to consume — paste it into an assistant before asking it for help with this
  panel. It includes a diagnosis priority order, because the default reflex when
  told "half my screen is noise" is to investigate wiring, which is exactly the
  wrong first move here.

Everything stated here was observed on physical hardware: the bare module on a
NodeMCU v3, and previously an ESP32-2432S028.

## License

MIT — see [LICENSE](LICENSE).
