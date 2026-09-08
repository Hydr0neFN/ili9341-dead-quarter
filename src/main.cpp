// -------------------------------------------------------------------------
// ILI9341 vs ILI9341_2 panel-init demo -- NodeMCU v3 (ESP8266) + 2.8" SPI TFT
//
// This sketch is deliberately identical across both PlatformIO environments.
// The ONLY difference is which driver variant TFT_eSPI compiles in, so any
// difference you see on the glass is caused by the init/register sequence
// and nothing else.
//
// What to look for when the wrong variant is compiled in:
//   * roughly the LAST QUARTER of the panel's memory never gets written -- it
//     shows static noise (whatever was left in GRAM), not the fill colour, and
//     it never changes no matter what the sketch draws
//   * physically it is always the same edge: the one farthest from the pin
//     header. Which way it LOOKS depends only on setRotation() -- a band across
//     the bottom in portrait, a strip down one side in landscape. Same defect.
//   * because of that, the pass/fail markers below are placed at BOTH the far
//     row and the far column. A marker in only one axis passes in the rotation
//     where the dead region happens to sit on the other axis.
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

// Fills the screen in four horizontal bands. Any band that is missing, or that
// does not reach the far edge, is uninitialised GRAM -- the controller never
// accepted those writes. On a mis-initialised clone the LAST band is the one
// that goes, because it is the last region written.
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

  // Two pass/fail indicators, one per axis. The dead region sits at the panel
  // edge farthest from the pin header, which lands on a different axis
  // depending on rotation -- a single marker would pass in half the rotations
  // while the panel is still broken. Both must be fully readable.
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
  // 1. Banded fill: shows dead columns at a glance, no text needed.
  drawBandedFill();
  delay(2500);

  // 2. Full report in portrait, then in landscape.
  for (uint8_t rot = 0; rot < 4; rot++) {
    tft.setRotation(rot);
    drawReport(rot);
    delay(3000);
  }
}
