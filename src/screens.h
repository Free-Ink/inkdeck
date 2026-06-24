#pragma once

// Screen render functions (pure draw-from-state) and the input entry points.
// Built entirely on the FreeInkApp Screen builder + FreeInkUI components.

#include <BleKeyboardHost.h>
#include <FreeInkApp.h>
#include <FreeInkUI.h>

#include "AppSettings.h"
#include "AppState.h"

// MaxInteractions must match the App template in main.cpp.
using ScreenT = freeink::ui::Screen<32>;

// --- Screen render functions (registered with app.setScreen) ---
void bootScreen(ScreenT& s, void* user);
void browserScreen(ScreenT& s, void* user);
void editorScreen(ScreenT& s, void* user);
void menuScreen(ScreenT& s, void* user);
void orientationScreen(ScreenT& s, void* user);
void nameEntryScreen(ScreenT& s, void* user);
void bluetoothScreen(ScreenT& s, void* user);
void sleepScreen(ScreenT& s, void* user);
void confirmDeleteScreen(ScreenT& s, void* user);
void confirmDiscardScreen(ScreenT& s, void* user);

// --- Input (called from the main loop) ---
void handleKey(const freeink::KeyEvent& ev);
void handleButton(uint8_t button);

// --- Lifecycle helpers ---
void loadDir();             // (re)read the current directory into g_state
void refreshStatusLabel();  // update the header connection label
void showToast(const char* msg);  // transient bottom popup message

// --- Implemented in main.cpp (decouple screens from the App template) ---
void uiGotoScreen(AppScreen s);    // setScreen + full-refresh invalidate
void uiInvalidate(bool full);      // request a re-render
void uiApplyOrientation();         // rebuild the UI target after orientation changes
void uiDrawHardwareFooter(const char* a, const char* b, const char* c, const char* d);
