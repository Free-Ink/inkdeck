// InkDeck — minimal markdown writing firmware for the Xteink X3/X4.
//
// Boots, auto-detects X3 vs X4, mounts the SD card, brings up the BLE HID host,
// and runs a FreeInkApp UI: a file browser, a plain-text markdown editor,
// file/folder management, and a Bluetooth pairing screen. Hardware buttons
// (Up/Down/Left/Right/Confirm/Back) navigate everything too.

#include <Arduino.h>
#include <BleKeyboardHost.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <FreeInkApp.h>
#include <FreeInkUI.h>
#include <FreeInkUIDisplayTarget.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <string.h>

#include "AppState.h"
#include "AppSettings.h"
#include "InkDeckIcons.h"
#include "screens.h"

namespace ui = freeink::ui;
using App = ui::FreeInkApp<32, 8>;

// ---------------------------------------------------------------------------
// Globals (definitions for the externs in AppState.h)
// ---------------------------------------------------------------------------
AppState g_state;
ui::DisplayTarget* g_target = nullptr;
ui::ThemeTokens g_theme;

namespace {

// Display pins are shared by X3 and X4 (BoardConfig XTEINK_X4 == X3 wiring).
EInkDisplay display(BoardConfig::XTEINK_X4.display.sclk, BoardConfig::XTEINK_X4.display.mosi,
                    BoardConfig::XTEINK_X4.display.cs, BoardConfig::XTEINK_X4.display.dc,
                    BoardConfig::XTEINK_X4.display.rst, BoardConfig::XTEINK_X4.display.busy);
InputManager input;
App* g_app = nullptr;

uint8_t g_fastCount = 0;            // partial refreshes since the last full
unsigned long g_btnIgnoreUntil = 0;  // swallow the ADC release ramp after Confirm/Back
uint8_t g_btLastDevCount = 0;        // repaint the BT list only when it changes
bool g_btWasScanning = false;
uint32_t g_btLastScanPaint = 0;
uint32_t g_allowSleepAt = 0;
bool g_powerSleepArmed = true;
static constexpr uint32_t kPowerSleepHoldMs = 1500;

bool usesHardwareFooter(AppScreen screen) {
  return screen != AppScreen::Boot && screen != AppScreen::Sleep;
}

ui::DeviceContext deviceFor(AppScreen screen) {
  ui::DeviceContext device = g_target->deviceContext();
  if (usesHardwareFooter(screen)) {
    const int16_t footer = g_theme.footerHeight;
    switch (device.orientation) {
      case ui::Orientation::LandscapeClockwise:
        device.safeArea = ui::Insets{0, 0, 0, footer};
        break;
      case ui::Orientation::PortraitInverted:
        device.safeArea = ui::Insets{footer, 0, 0, 0};
        break;
      case ui::Orientation::LandscapeCounterClockwise:
        device.safeArea = ui::Insets{0, footer, 0, 0};
        break;
      case ui::Orientation::Portrait:
      default:
        device.safeArea = ui::Insets{0, 0, footer, 0};
        break;
    }
  }
  return device;
}

App::ScreenFn fnFor(AppScreen s) {
  switch (s) {
    case AppScreen::Boot:
      return bootScreen;
    case AppScreen::Browser:
      return browserScreen;
    case AppScreen::Editor:
      return editorScreen;
    case AppScreen::Menu:
      return menuScreen;
    case AppScreen::Orientation:
      return orientationScreen;
    case AppScreen::NameEntry:
      return nameEntryScreen;
    case AppScreen::Bluetooth:
      return bluetoothScreen;
    case AppScreen::Sleep:
      return sleepScreen;
    case AppScreen::ConfirmDelete:
      return confirmDeleteScreen;
    case AppScreen::ConfirmDiscard:
      return confirmDiscardScreen;
  }
  return browserScreen;
}

void createUi(AppScreen screen) {
  delete g_app;
  delete g_target;
  g_target = new ui::DisplayTarget(display.getFrameBuffer(), display.getDisplayWidth(), display.getDisplayHeight(),
                                   display.getDisplayWidthBytes(), g_settings.uiOrientation());
  g_state.screen = screen;
  g_app = new App(*g_target, deviceFor(screen));
  g_app->setTheme(g_theme);
  g_app->setScreen(fnFor(screen));
}

void pushRefresh() {
  bool full = false;
  switch (g_app->lastRenderRefreshHint()) {
    case ui::RefreshHint::Full:
    case ui::RefreshHint::Clean:
      full = true;
      break;
    case ui::RefreshHint::Fast:
      if (++g_fastCount >= 40) full = true;  // occasional ghost-clearing full
      break;
    case ui::RefreshHint::None:
      return;
  }

  if (full) {
    display.displayBuffer(EInkDisplay::FULL_REFRESH);
    // The first differential after a full garbles / lags one frame on the X3
    // (UC8253) — so the just-drawn frame would otherwise appear "one paint
    // behind". Prime with one FAST of the SAME frame so the displayed image is
    // correct and the next interactive refresh is clean. (escape-hatch fix.)
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    g_fastCount = 0;
  } else {
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Helpers exposed to screens.cpp (decouple it from the App template)
// ---------------------------------------------------------------------------
void uiGotoScreen(AppScreen s) {
  // Snappy partial-refresh transitions (RefreshHint::Fast) — the SDK now lets
  // setScreen pick the hint, so screen changes don't force a slow full refresh
  // (which on the X3 also lagged a frame). Boot/ghost-clearing fulls are primed
  // in pushRefresh().
  g_state.screen = s;
  g_app->setDevice(deviceFor(s));
  g_app->setScreen(fnFor(s), nullptr, ui::RefreshHint::Fast);
}

void uiInvalidate(bool full) {
  g_app->invalidate(full ? ui::RefreshHint::Full : ui::RefreshHint::Fast);
}

void uiApplyOrientation() {
  createUi(g_state.screen);
  uiInvalidate(true);
}

ui::BitmapRef bitmapForIcon(const freeink::Icon& icon) {
  return ui::BitmapRef{icon.bits, icon.w, icon.h, ui::BitmapFormat::Mask1};
}

ui::Rotation iconRotationForOrientation() {
  switch (g_settings.uiOrientation()) {
    case ui::Orientation::LandscapeClockwise:
      return ui::Rotation::CCW90;
    case ui::Orientation::PortraitInverted:
      return ui::Rotation::R180;
    case ui::Orientation::LandscapeCounterClockwise:
      return ui::Rotation::CW90;
    case ui::Orientation::Portrait:
    default:
      return ui::Rotation::None;
  }
}

ui::BitmapRef footerIconForLabel(const char* label) {
  if (!label || !label[0]) return {};
  if (strcmp(label, "Menu") == 0) return bitmapForIcon(icon_menu_24);
  if (strcmp(label, "Back") == 0) return bitmapForIcon(icon_back_24);
  if (strcmp(label, "Open") == 0) return bitmapForIcon(icon_open_24);
  if (strcmp(label, "Select") == 0) return bitmapForIcon(icon_select_24);
  if (strcmp(label, "Save") == 0) return bitmapForIcon(icon_save_24);
  if (strcmp(label, "Up") == 0) return bitmapForIcon(icon_up_24);
  if (strcmp(label, "Down") == 0) return bitmapForIcon(icon_down_24);
  if (strcmp(label, "Cancel") == 0) return bitmapForIcon(icon_cancel_24);
  if (strcmp(label, "OK") == 0) return bitmapForIcon(icon_ok_24);
  return {};
}

void uiDrawHardwareFooter(const char* a, const char* b, const char* c, const char* d) {
  ui::DisplayTarget target(display.getFrameBuffer(), display.getDisplayWidth(), display.getDisplayHeight(),
                           display.getDisplayWidthBytes(), ui::Orientation::Portrait);
  const int16_t width = target.logicalWidth();
  const int16_t height = target.logicalHeight();
  const int16_t footer = g_theme.footerHeight;
  const int16_t y = static_cast<int16_t>(height - footer);

  target.fill(ui::Rect{0, y, width, footer}, ui::Paint::solid(ui::Color::White));

  const char* labels[] = {a, b, c, d};
  const int16_t sidePadding = 8;
  const int16_t gap = 4;
  const int16_t contentW = static_cast<int16_t>(width - sidePadding * 2);
  const int16_t totalGap = gap * 3;
  const int16_t slotW = static_cast<int16_t>((contentW - totalGap) / 4);
  int16_t x = sidePadding;

  ui::TextStyle text = g_theme.bodyText;
  text.align = ui::TextAlign::Center;
  text.maxLines = 1;
  const ui::Rotation iconRotation = iconRotationForOrientation();

  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t w = i == 3 ? static_cast<int16_t>(width - sidePadding - x) : slotW;
    ui::Rect rect{x, y, w, footer};
    if (labels[i] && labels[i][0]) {
      const ui::BitmapRef icon = footerIconForLabel(labels[i]);
      if (icon) {
        target.bitmap(rect.inset(ui::Insets{4, 4, 4, 4}), icon, ui::BitmapMode::Center,
                      ui::Paint::solid(ui::Color::Black), iconRotation);
      } else {
        target.text(rect.inset(ui::Insets{4, 4, 4, 4}), labels[i], text);
      }
    }
    x = static_cast<int16_t>(x + w + gap);
  }
}

[[noreturn]] void enterDeepSleep() {
  BleHid.stopScan();
  BleHid.disconnect();
  g_state.toast[0] = '\0';
  g_state.toastUntil = 0;
  g_state.screen = AppScreen::Sleep;
  g_app->setScreen(fnFor(AppScreen::Sleep), nullptr, ui::RefreshHint::Full);
  display.clearScreen(0xFF);
  g_app->render();
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  display.deepSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

bool prepareSleep() {
  if (g_state.screen == AppScreen::Editor && g_state.doc.dirty() && !g_state.doc.save()) {
    showToast("Save failed");
    return false;
  }
  enterDeepSleep();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  const bool isX3 = freeink::selectXteinkDevice();

  // X3/X4 share the display SPI bus with the SD card; claim it once (with MISO)
  // before SD begin, exactly like escape-hatch.
  SPI.begin(BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.sd.miso, BoardConfig::ACTIVE.display.mosi,
            BoardConfig::ACTIVE.display.cs);

  if (isX3) display.setDisplayX3();
  display.begin();
  delay(50);

  input.begin();
  input.beginAsync();  // latch presses on a background task across slow refreshes

  g_settings.load();
  g_theme = ui::defaultThemeTokens();
  g_theme.headerHeight = 52;
  g_theme.footerHeight = 48;
  g_theme.rowHeight = 46;

  createUi(AppScreen::Boot);
  display.clearScreen(0xFF);
  g_app->render();
  pushRefresh();

  const bool sdOk = SdMan.begin();
  BleHid.begin("InkDeck");
  refreshStatusLabel();
  g_state.lastConnected = BleHid.isConnected();

  loadDir();
  // Boot paint: default Full refresh to seed the panel's differential base
  // (pushRefresh primes a FAST right after). Subsequent transitions are Fast.
  g_state.screen = AppScreen::Browser;
  g_app->setDevice(deviceFor(AppScreen::Browser));
  g_app->setScreen(fnFor(AppScreen::Browser));
  if (!sdOk) showToast("SD card not detected");

  // Absorb the wake press so holding Power during boot cannot immediately
  // satisfy the sleep hold threshold.
  freeink::PowerManager::waitForPowerButtonRelease();
  g_allowSleepAt = millis() + 2000;
  g_powerSleepArmed = true;
}

void loop() {
  BleHid.poll();
  char bleFailure[48];
  if (BleHid.takeConnectFailure(bleFailure, sizeof(bleFailure))) {
    showToast(bleFailure);
    uiInvalidate(false);
  }
  uint32_t passkey = 0;
  if (BleHid.takePairingPasskey(passkey)) {
    char msg[32];
    snprintf(msg, sizeof(msg), "Type %06lu Enter", static_cast<unsigned long>(passkey));
    showToast(msg);
    g_state.toastUntil = millis() + 30000;
    uiInvalidate(false);
  }

  // Reflect connection changes in the header.
  const bool connected = BleHid.isConnected();
  if (connected != g_state.lastConnected) {
    g_state.lastConnected = connected;
    refreshStatusLabel();
    uiInvalidate(false);
  }

  // BLE HID input events.
  freeink::KeyEvent ev;
  while (BleHid.popKey(ev)) handleKey(ev);

  // Hardware buttons.
  uint8_t b;
  while (input.popPress(b)) {
    if (millis() < g_btnIgnoreUntil) continue;  // discard release-ramp artifacts
    handleButton(b);
    if (b == InputManager::BTN_CONFIRM || b == InputManager::BTN_BACK) g_btnIgnoreUntil = millis() + 250;
  }

  if (!input.isPowerButtonPressed()) g_powerSleepArmed = true;
  if (g_powerSleepArmed && millis() >= g_allowSleepAt && input.isPowerButtonPressed() &&
      input.getPowerButtonHeldTime() >= kPowerSleepHoldMs) {
    g_powerSleepArmed = false;
    if (!prepareSleep()) g_allowSleepAt = millis() + 1000;
  }

  // Keep active BLE scans quiet. Repainting e-ink for every background
  // advertiser can stall the host long enough to miss scan responses.
  if (g_state.screen == AppScreen::Bluetooth) {
    const uint8_t dc = BleHid.deviceCount();
    const bool sc = BleHid.isScanning();
    const uint32_t now = millis();
    const bool scanStateChanged = sc != g_btWasScanning;
    const bool shouldPaintScan = sc && dc != g_btLastDevCount && now - g_btLastScanPaint >= 3000;
    const bool shouldPaintIdle = !sc && dc != g_btLastDevCount;
    if (scanStateChanged || shouldPaintScan || shouldPaintIdle) {
      g_btLastDevCount = dc;
      g_btWasScanning = sc;
      g_btLastScanPaint = now;
      uiInvalidate(false);
    }
  }

  // Toasts are time-based, but e-ink only changes when we explicitly repaint.
  // Clear and repaint once when the deadline passes so the popup disappears.
  if (g_state.toast[0] && static_cast<int32_t>(millis() - g_state.toastUntil) >= 0) {
    g_state.toast[0] = '\0';
    g_state.toastUntil = 0;
    uiInvalidate(false);
  }

  if (g_app->invalidated()) {
    display.clearScreen(0xFF);
    g_app->render();
    pushRefresh();
  }

  delay(8);
}
