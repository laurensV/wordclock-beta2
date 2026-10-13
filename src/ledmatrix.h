#ifndef ledmatrix_h
#define ledmatrix_h

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "../wordclock.h"
#include "font.h"

#define RED Adafruit_NeoPixel::Color(255, 0, 0)
#define GREEN Adafruit_NeoPixel::Color(0, 255, 0)
#define BLUE Adafruit_NeoPixel::Color(0, 0, 255)
#define WHITE Adafruit_NeoPixel::Color(255, 255, 255)
#define BLACK Adafruit_NeoPixel::Color(0, 0, 0)

class LEDMatrix {
public:
  enum LED_TYPE { OFF, TIME, NAME, ICON, OTHER };
  enum CORNER { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };
  LEDMatrix(Adafruit_NeoPixel* pixels);
  void setPixelType(uint8_t row, uint8_t col, enum LED_TYPE type);
  void clear(void);
  void draw(bool show);
  uint16_t pixelIndex(uint8_t row, uint8_t col);
  uint8_t pixelRow(uint16_t i);
  uint8_t pixelCol(uint16_t i);
  bool hasLed(uint8_t row, uint8_t col);
  bool isLetter(uint8_t row, uint8_t col);
  bool boxHasLeds(uint8_t xpos, uint8_t ypos);
  bool fontFits(uint8_t xpos, uint8_t ypos);
  bool glyphFits(const byte* glyph, uint8_t xpos, uint8_t ypos);
  bool ipFits(uint8_t xpos, uint8_t ypos);
  bool cornerSlot(enum CORNER corner, uint8_t* xpos, uint8_t* ypos);
  bool ipSlot(uint8_t* xpos, uint8_t* ypos);
  void print(const byte* ledbuffer, uint8_t xpos, uint8_t ypos, enum LED_TYPE type);
  enum LED_TYPE leds[20][20] = { OFF };

private:
  Adafruit_NeoPixel* pixels;
  bool findSlot(enum CORNER corner, uint8_t width, bool (LEDMatrix::*fits)(uint8_t, uint8_t), uint8_t* xpos, uint8_t* ypos);
};

#endif