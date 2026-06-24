#pragma once

#include <Arduino.h>
#include <FreeInkUI.h>

struct AppSettings {
  enum Orientation : uint8_t {
    Portrait = 0,
    LandscapeClockwise = 1,
    PortraitInverted = 2,
    LandscapeCounterClockwise = 3,
  };

  uint8_t orientation = Portrait;

  void load();
  void save() const;
  freeink::ui::Orientation uiOrientation() const;
  const char* orientationLabel() const;
};

extern AppSettings g_settings;
