#include "ledmatrix.h"

/**
 * @brief Construct a new LEDMatrix::LEDMatrix object
 *
 * @param neopixels pointer to Adafruit_NeoPixel object
 */
LEDMatrix::LEDMatrix(Adafruit_NeoPixel* neopixels) {
  pixels = neopixels;
}

/**
 * @brief Set a pixel in led matrix to a color type
 *
 * @param row row of pixel
 * @param col column of pixel
 * @param type type of pixel
 */
void LEDMatrix::setPixelType(uint8_t row, uint8_t col, enum LED_TYPE type) {
  // limit ranges of row and col
  if (row < clockHeight && col < clockWidth) {
    leds[row][col] = type;
  }
}

/**
 * @brief "Deactivates" all pixels in matrix
 *
 */
void LEDMatrix::clear(void) {
  // set a zero to each pixel
  for (uint8_t row = 0; row < clockHeight; row++) {
    for (uint8_t col = 0; col < clockWidth; col++) {
      leds[row][col] = OFF;
    }
  }
}

/**
 * @brief Draws the led matrix to the neopixels
 */
void LEDMatrix::draw(bool show) {
  // loop over all leds in matrix
  for (int row = 0; row < clockHeight; row++) {
    for (int col = 0; col < clockWidth; col++) {
      uint32_t color;
      switch (leds[row][col]) {
        case OFF:
          color = BLACK;
          break;
        case TIME:
          color = color_TIME;
          if (mode == RAINBOW) {
            // force time to white during rainbow
            color = WHITE;
          }
          break;
        case NAME:
          color = color_NAME;
          if (mode == RAINBOW) {
            // force time to white during rainbow
            color = WHITE;
          }
          break;
        case ICON:
          color = color_ICON;
          if (mode == RAINBOW) {
            // force time to white during rainbow
            color = WHITE;
          }
          break;
        case OTHER:
          color = GREEN;
          break;
      }
      pixels->setPixelColor(pixelIndex(row, col), color);
    }
  }
  if (show) {
    pixels->show();
  }
}

/**
 * @brief Whether there is a (usable) led at this position.
 * A space or '.' in the clock layout means: no letter here and the led behind
 * it must never be lit (for example because a picture covers this part).
 */
bool LEDMatrix::hasLed(uint8_t row, uint8_t col) {
  if (row >= clockHeight || col >= clockWidth) {
    return false;
  }
  uint16_t i = row * clockWidth + col;
  if (i >= clockLayout.length()) {
    // no (complete) layout configured: assume a full grid
    return true;
  }
  char c = clockLayout.charAt(i);
  return c != ' ' && c != '.';
}

/**
 * @brief Whether this position is a normal letter, so it can be used to draw
 * digits on. Name ('*') and icon ('&') leds are excluded.
 */
bool LEDMatrix::isLetter(uint8_t row, uint8_t col) {
  if (!hasLed(row, col)) {
    return false;
  }
  uint16_t i = row * clockWidth + col;
  if (i >= clockLayout.length()) {
    return true;
  }
  char c = clockLayout.charAt(i);
  return c != '*' && c != '&';
}

/**
 * @brief Whether every position of a FONT_WIDTH x FONT_HEIGHT box with its
 * top-left corner at column xpos, row ypos has a led. Used to decide whether
 * the classic centered digits can still be used on this layout.
 */
bool LEDMatrix::boxHasLeds(uint8_t xpos, uint8_t ypos) {
  for (uint8_t y = 0; y < FONT_HEIGHT; y++) {
    for (uint8_t x = 0; x < FONT_WIDTH; x++) {
      if (!hasLed(ypos + y, xpos + x)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Whether a full FONT_WIDTH x FONT_HEIGHT character fits on letters
 * when drawn with its top-left corner at column xpos, row ypos.
 */
bool LEDMatrix::fontFits(uint8_t xpos, uint8_t ypos) {
  for (uint8_t y = 0; y < FONT_HEIGHT; y++) {
    for (uint8_t x = 0; x < FONT_WIDTH; x++) {
      if (!isLetter(ypos + y, xpos + x)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Whether the lit pixels of a glyph all land on letters when drawn
 * with its top-left corner at column xpos, row ypos. Unlit pixels of the
 * glyph may be anywhere (e.g. on a gap), which is less strict than fontFits.
 */
bool LEDMatrix::glyphFits(const byte* glyph, uint8_t xpos, uint8_t ypos) {
  for (uint8_t i = 0; i < FONT_HEIGHT; i++) {
    for (uint8_t j = 0; j < FONT_WIDTH; j++) {
      if ((glyph[i] & 1 << j) && !isLetter(i + ypos, FONT_WIDTH - j - 1 + xpos)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Whether "IP" (narrow I, space, P: FONT_IP_WIDTH columns) fits with
 * its top-left corner at column xpos, row ypos.
 */
bool LEDMatrix::ipFits(uint8_t xpos, uint8_t ypos) {
  return glyphFits(FONT_I_NARROW, xpos, ypos) && glyphFits(FONT_P, xpos + 2, ypos);
}

/**
 * @brief Find the position closest to the given corner of the clock where a
 * character of the font fits completely on letters. Used when the middle of
 * the clock is not available (e.g. a picture in the middle).
 *
 * @return true if a slot was found, with its top-left column/row in xpos/ypos
 */
bool LEDMatrix::cornerSlot(enum CORNER corner, uint8_t* xpos, uint8_t* ypos) {
  return findSlot(corner, FONT_WIDTH, &LEDMatrix::fontFits, xpos, ypos);
}

/**
 * @brief Find the position closest to the top-left corner where "IP" fits.
 */
bool LEDMatrix::ipSlot(uint8_t* xpos, uint8_t* ypos) {
  return findSlot(TOP_LEFT, FONT_IP_WIDTH, &LEDMatrix::ipFits, xpos, ypos);
}

/**
 * @brief Search outwards from a corner for the closest position where a
 * `width` columns wide, FONT_HEIGHT rows high drawing fits according to `fits`.
 */
bool LEDMatrix::findSlot(enum CORNER corner, uint8_t width, bool (LEDMatrix::*fits)(uint8_t, uint8_t), uint8_t* xpos, uint8_t* ypos) {
  if (clockWidth < width || clockHeight < FONT_HEIGHT) {
    return false;
  }
  bool right = corner == TOP_RIGHT || corner == BOTTOM_RIGHT;
  bool bottom = corner == BOTTOM_LEFT || corner == BOTTOM_RIGHT;
  uint8_t maxX = clockWidth - width;
  uint8_t maxY = clockHeight - FONT_HEIGHT;
  // only search in the quarter of the clock that belongs to this corner
  // (strictly left/right of the middle, above/below the middle), so the
  // slots of different corners can never overlap
  uint8_t halfW = clockWidth / 2;
  uint8_t halfH = clockHeight / 2;
  uint8_t maxDx = right ? (clockWidth - halfW <= maxX ? maxX - (clockWidth - halfW) : 0) : (halfW >= width ? halfW - width : 0);
  uint8_t maxDy = bottom ? (clockHeight - halfH <= maxY ? maxY - (clockHeight - halfH) : 0) : (halfH >= FONT_HEIGHT ? halfH - FONT_HEIGHT : 0);
  // search outwards from the corner, closest slot first
  for (uint8_t d = 0; d <= maxDx + maxDy; d++) {
    for (uint8_t dy = 0; dy <= d && dy <= maxDy; dy++) {
      uint8_t dx = d - dy;
      if (dx > maxDx) {
        continue;
      }
      uint8_t x = right ? maxX - dx : dx;
      uint8_t y = bottom ? maxY - dy : dy;
      if ((this->*fits)(x, y)) {
        *xpos = x;
        *ypos = y;
        return true;
      }
    }
  }
  return false;
}

/**
 * shows a byte buffer with its top-left corner at column xpos, row ypos
 */
void LEDMatrix::print(const byte* drawing, uint8_t xpos, uint8_t ypos, enum LED_TYPE type) {
  for (int i = 0; i < FONT_HEIGHT; ++i) {
    for (int j = 0; j < FONT_WIDTH; ++j) {
      if (drawing[i] & 1 << j) {
        setPixelType(i + ypos, FONT_WIDTH - j - 1 + xpos, type);
      }
    }
  }
  // for (int y = ypos, i = 0; y < (ypos + 5); y++, i++) {
  //   for (int x = xpos, k = 2; x < (xpos + 3); x++, k--) {
  //     if ((drawing[i] >> k) & 0x1) {
  //       setPixelType(x, y, type);
  //     }
  //   }
  // }
}

uint16_t LEDMatrix::pixelIndex(uint8_t row, uint8_t col) {
  if (row % 2) {
    // Odd row, right to left
    return row * clockWidth + (clockWidth - 1 - col);
  }
  // Even row, left to right
  return row * clockWidth + col;
}

uint8_t LEDMatrix::pixelRow(uint16_t i) {
  return i / clockWidth;
}
uint8_t LEDMatrix::pixelCol(uint16_t i) {
  uint8_t col = i % clockWidth;
  if ((i / clockWidth) % 2) {
    // Odd row, right to left
    col = clockWidth - 1 - col;
  }
  return col;
}