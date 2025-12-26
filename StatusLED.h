#pragma once
#include <WiFi.h>
#include <Matter.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"

class StatusIndicator {
public:
  StatusIndicator(uint8_t count, uint8_t pin) 
    : _pixel(count, pin, NEO_GRB + NEO_KHZ800) {}

  void begin() {
    _pixel.begin();
    _pixel.clear();
    _pixel.show();
  }

  /**
   * Update the LED status based on the device state.
   * Call this frequently in loop().
   */
  void update() {
    if (!Matter.isDeviceCommissioned()) {
        breathe(150, 150, 150); // White
    } else if (WiFi.status() != WL_CONNECTED) {
        set(180, 50, 0); // More distinct Orange/Amber
    } else {
        set(0, 40, 0);   // Brighter Green to avoid yellow tint
    }
  }

private:
  void set(uint8_t r, uint8_t g, uint8_t b) {
    _pixel.setPixelColor(0, _pixel.Color(r, g, b));
    _pixel.show();
  }

  void breathe(uint8_t r, uint8_t g, uint8_t b) {
    float brightness = (sin(millis() / 400.0) + 1) / 2.0;
    uint8_t vr = (uint8_t)(brightness * r) + 10;
    uint8_t vg = (uint8_t)(brightness * g) + 10;
    uint8_t vb = (uint8_t)(brightness * b) + 10;
    _pixel.setPixelColor(0, _pixel.Color(vr, vg, vb));
    _pixel.show();
  }

  Adafruit_NeoPixel _pixel;
};
