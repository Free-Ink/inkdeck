#include "AppSettings.h"

#include <Preferences.h>

AppSettings g_settings;

namespace {
constexpr const char* kPrefsNs = "inkdeck";
constexpr const char* kOrientationKey = "orient";
}  // namespace

void AppSettings::load() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) return;
  orientation = prefs.getUChar(kOrientationKey, Portrait);
  prefs.end();
  if (orientation > LandscapeCounterClockwise) orientation = Portrait;
}

void AppSettings::save() const {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) return;
  prefs.putUChar(kOrientationKey, orientation);
  prefs.end();
}

freeink::ui::Orientation AppSettings::uiOrientation() const {
  switch (orientation) {
    case LandscapeClockwise:
      return freeink::ui::Orientation::LandscapeClockwise;
    case PortraitInverted:
      return freeink::ui::Orientation::PortraitInverted;
    case LandscapeCounterClockwise:
      return freeink::ui::Orientation::LandscapeCounterClockwise;
    case Portrait:
    default:
      return freeink::ui::Orientation::Portrait;
  }
}

const char* AppSettings::orientationLabel() const {
  switch (orientation) {
    case LandscapeClockwise:
      return "Landscape CW";
    case PortraitInverted:
      return "Inverted";
    case LandscapeCounterClockwise:
      return "Landscape CCW";
    case Portrait:
    default:
      return "Portrait";
  }
}
