# InkDeck

A super-minimal **markdown writing** firmware for the Xteink **X3/X4** (ESP32-C3
e-ink devices). Pair a Bluetooth HID input device, write `.md` files to the SD
card, and manage them — create/rename/delete files and folders — from the device
buttons or a connected keyboard.

Built entirely on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk):
`FreeInkApp`/`FreeInkUI` for the UI, `SDCardManager` for storage, `XteinkDetect`
for X3/X4 auto-detection, and two pieces added to the SDK for this firmware:

- **`BleKeyboardHost`** (`libs/network/BleKeyboardHost`) — a BLE HID host for
  keyboards, page turners, and similar peripherals, capability-gated by
  `FREEINK_CAP_BLE_HID_HOST`. See the SDK's
  `docs/ble-keyboard-host.md`.
- **`textArea`** — a multi-line scrolling editor component in FreeInkUI.

> **Bluetooth is BLE only.** The ESP32-C3 has no Bluetooth Classic radio, so
> Classic-only keyboards cannot connect. Most modern wireless/compact keyboards
> and page turners are BLE.

## Using it

- **Browser** — the SD card file list (folders first, then `.md` files). Navigate
  with the device buttons or a connected keyboard's arrows/Enter; `Back` leaves a
  folder.
  - **New file / New folder / Rename / Delete** via the on-screen menu.
  - **Bluetooth** screen: scan, pair, connect, and forget HID devices.
- **Editor** — type markdown as plain text with soft word-wrap and a caret.
  - `Ctrl-S` save · `Esc` back to the browser (prompts if unsaved) · arrows /
    Home / End / PageUp / PageDown to move · Backspace / Delete to edit.
- **Name entry** (new file/folder, rename) **requires a connected keyboard** —
  you'll be prompted to pair one first.

## Building

This project consumes the FreeInk SDK via PlatformIO `symlink://` lib deps,
exactly like the `escape-hatch` example. For local development the committed
`platformio.local.ini` overrides the lib paths with absolute symlinks to a
working copy of the SDK at `/Users/jmitch/GitHub/freeink-sdk`, so SDK changes
iterate without committing anything.

```sh
pio run                 # build (env: default — X3 + X4 in one binary)
pio run -t upload       # flash over USB
pio device monitor      # serial @ 115200
```

The committed `platformio.ini` references the SDK as a `freeink-sdk/` checkout.
The `BleKeyboardHost` library and the FreeInkUI `textArea` component live in the
SDK working copy; once those are upstreamed, add the SDK as a submodule and the
committed config builds without the local override.

## Layout

```
src/
  main.cpp      lifecycle, input routing, refresh policy (FreeInkApp loop)
  AppState.h    screen enum + shared app state
  Document.*    the open file: fixed-capacity in-RAM text buffer + load/save
  screens.*     browser, editor, file-ops menu, name entry, bluetooth screens
```
