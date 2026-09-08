// -------------------------------------------------------------------------
// ILI9341 vs ILI9341_2 panel-init demo -- NodeMCU v3 (ESP8266) + 2.8" SPI TFT
//
// This sketch is deliberately identical across both PlatformIO environments.
// The ONLY difference is which driver variant TFT_eSPI compiles in, so any
// difference you see on the glass is caused by the init/register sequence
// and nothing else.
//
// What to look for when the wrong variant is compiled in (all CONFIRMED on
// hardware, see docs/img/):
//   * roughly a QUARTER of the physical panel is never written. It keeps
//     showing whatever was drawn before -- old text under new colour bands,
//     old colour bands beside new text -- and never updates.
//   * content that should be near the end of the canvas (LAST-ROW OK, the
//     BL/BR corner labels) appears near the START of the panel instead: the
//     writes run past the addressable area and wrap around.
//   * the dead strip MOVES BETWEEN ROTATIONS. setRotation() rewrites MADCTL,
//     which changes the scan direction, so the untouched region jumps to a
//     different physical edge. It is not fixed to one side of the board.
//
// IMPORTANT -- why the on-screen markers below cannot detect this:
//   The driver's own 240x320 canvas stays internally consistent. Every marker
//   drawn in driver coordinates -- RIGHT-EDGE OK, LAST-ROW OK, the ruler's
//   239 label -- renders correctly and reads as a PASS while the panel is
//   plainly broken. What is wrong is where that canvas lands on the glass.
//   Only drawWipeTest() below catches this class of fault, because it tests
//   the one thing the driver cannot fake: whether fillScreen() actually
//   covers the whole physical panel.
//
// The asymmetric "FR7" text and the corner labels are there to expose a
// second, independent failure: a mirrored MADCTL. Symmetric test patterns
// cannot reveal that -- text can.
// -------------------------------------------------------------------------

#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#if defined(ILI9341_2_DRIVER)
  static const char *DRIVER_NAME = "ILI9341_2_DRIVER";
  static const char *DRIVER_HINT = "clone register set - EXPECT FULL SCREEN";
#elif defined(ILI9341_DRIVER)
  static const char *DRIVER_NAME = "ILI9341_DRIVER";
  static const char *DRIVER_HINT = "stock register set - EXPECT DEAD COLUMNS";
#else
  static const char *DRIVER_NAME = "UNKNOWN DRIVER";
  static const char *DRIVER_HINT = "no ILI9341 variant defined";
#endif

// Draws a border 1px inside every edge plus a tick at each corner.
// If the panel is only partially addressed, the right edge simply is not
// there, which is far easier to see than a missing pixel row.
static void drawEdgeFrame(uint16_t colour) {
  const int16_t w = tft.width();
  const int16_t h = tft.height();

  tft.drawRect(0, 0, w, h, colour);
  tft.drawRect(1, 1, w - 2, h - 2, colour);

  const int16_t t = 16;  // corner tick length
  tft.fillRect(0, 0, t, 4, colour);
  tft.fillRect(w - t, 0, t, 4, colour);
  tft.fillRect(0, h - 4, t, 4, colour);
  tft.fillRect(w - t, h - 4, t, 4, colour);
}

// A vertical ruler across the full width, labelled every 40 px.
// The label nearest the right edge is the single most useful pixel on the
// screen: on a mis-initialised panel it never renders.
static void drawColumnRuler(int16_t y) {
  const int16_t w = tft.width();

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  for (int16_t x = 0; x < w; x += 10) {
    const bool major = (x % 40 == 0);
    tft.drawFastVLine(x, y, major ? 12 : 6, major ? TFT_YELLOW : TFT_DARKGREY);
    if (major) {
      tft.setCursor(x + 2, y + 14);
      tft.print(x);
    }
  }

  // explicit marker at the very last addressable column
  tft.drawFastVLine(w - 1, y, 12, TFT_RED);
  tft.setCursor(w - 26, y + 14);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.print(w - 1);
}

// THE test. Everything else on screen is diagnosis; this is the pass/fail.
//
// Fills the entire canvas with one flat colour, several times, alternating
// between two colours that cannot be confused with each other. If the driver
// and the panel agree on the geometry, the whole physical panel changes colour
// together, every time.
//
// If they do not agree, some region never takes the fill and keeps showing the
// previous screen. That region is outside the addressable window, and no amount
// of drawing will ever reach it. This is the only check here that the driver
// cannot pass while being wrong, because it tests coverage of the glass rather
// than consistency of the canvas.
static void drawWipeTest() {
  const uint16_t colours[] = {TFT_WHITE, TFT_NAVY, TFT_WHITE, TFT_NAVY};

  for (uint8_t i = 0; i < 4; i++) {
    tft.fillScreen(colours[i]);

    tft.setTextFont(2);
    tft.setTextColor(colours[i] == TFT_WHITE ? TFT_BLACK : TFT_WHITE);
    tft.setCursor(6, 6);
    tft.print(F("WIPE TEST"));
    tft.setTextFont(1);
    tft.setCursor(6, 26);
    tft.print(F("whole panel must be one flat colour"));
    tft.setCursor(6, 38);
    tft.print(F("any leftover patch = outside the window"));

    delay(900);
  }
}

// Fills the screen in four horizontal bands. Two jobs: it makes an unreachable
// region obvious without reading anything, and the bands persist into the next
// screen if the wipe cannot clear them -- which is exactly the symptom.
static void drawBandedFill() {
  const int16_t w = tft.width();
  const int16_t h = tft.height();
  const uint16_t bands[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_MAGENTA};
  const int16_t bandH = h / 4;

  for (uint8_t i = 0; i < 4; i++) {
    tft.fillRect(0, i * bandH, w, bandH, bands[i]);
  }
}

static void drawReport(uint8_t rotation) {
  const int16_t w = tft.width();
  const int16_t h = tft.height();

  tft.fillScreen(TFT_BLACK);
  drawEdgeFrame(TFT_CYAN);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(6, 8);
  tft.print(DRIVER_NAME);

  tft.setTextFont(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(6, 30);
  tft.print(DRIVER_HINT);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(6, 44);
  tft.printf("rotation=%u  %dx%d", rotation, w, h);

  // Asymmetric glyphs: these read backwards if MADCTL mirrors the panel.
  // A centred symmetric shape would look perfectly fine while mirrored.
  tft.setTextFont(4);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(6, 60);
  tft.print("FR7");
  tft.setTextFont(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(60, 70);
  tft.print("<- must read left to right");

  drawColumnRuler(96);

  // Two edge markers, one per axis. Read them as "the canvas is internally
  // consistent", NOT as "the panel is fine" -- both render perfectly on a
  // mis-initialised clone (see the header comment). They are here to catch a
  // driver that disagrees with itself about the canvas size, which is a
  // different fault from the one drawWipeTest() hunts.
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  const char *colMsg = "RIGHT-EDGE OK";
  tft.setCursor(w - tft.textWidth(colMsg) - 4, 130);
  tft.print(colMsg);

  const char *rowMsg = "LAST-ROW OK";
  tft.setCursor((w - tft.textWidth(rowMsg)) / 2, h - 34);
  tft.print(rowMsg);

  // A solid bar hard against the final row: the last thing written, so the
  // first thing to disappear.
  tft.fillRect(0, h - 8, w, 5, TFT_GREENYELLOW);

  // Corner identification, so a rotation problem is distinguishable from an
  // addressing problem.
  tft.setTextFont(1);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setCursor(4, h - 20);
  tft.print("BL");
  tft.setCursor(w - 18, h - 20);
  tft.print("BR");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== ILI9341 panel init demo ==="));
  Serial.printf("compiled driver : %s\n", DRIVER_NAME);
  Serial.printf("expectation     : %s\n", DRIVER_HINT);

  tft.init();
  tft.setRotation(0);  // native portrait 240x320

  Serial.printf("tft.width()     : %d\n", tft.width());
  Serial.printf("tft.height()    : %d\n", tft.height());
  Serial.println(F("If the glass disagrees with these numbers, the driver"));
  Serial.println(F("variant is wrong -- the MCU cannot detect that itself."));
}

void loop() {
  // 1. The actual pass/fail test. Run it in every rotation, because the
  //    unreachable region moves when setRotation() rewrites MADCTL -- a single
  //    rotation can look clean while another does not.
  for (uint8_t rot = 0; rot < 4; rot++) {
    tft.setRotation(rot);
    drawWipeTest();
  }

  // 2. Banded fill: an unreachable region is obvious without reading anything,
  //    and whatever survives here proves the wipe above could not clear it.
  tft.setRotation(0);
  drawBandedFill();
  delay(2500);

  // 3. Diagnosis detail -- ruler, edge markers, mirror check -- in each
  //    rotation. Useful once the wipe test passes; not a substitute for it.
  for (uint8_t rot = 0; rot < 4; rot++) {
    tft.setRotation(rot);
    drawReport(rot);
    delay(3000);
  }
}
