#include "screens.h"

#include <InputManager.h>
#include <SDCardManager.h>
#include <stdlib.h>
#include <string.h>

using namespace freeink::ui;
using freeink::KeyEvent;
using freeink::SpecialKey;

// HID modifier bits (mirrors BleKeyboardHost's internal HidMod; the lib's
// HidKeymap.h isn't on the firmware include path).
static constexpr uint8_t kModCtrl = 0x01 | 0x10;   // L/R Ctrl
static constexpr uint8_t kModShift = 0x02 | 0x20;  // L/R Shift
static constexpr uint8_t kModAlt = 0x04 | 0x40;    // L/R Alt / Option
static constexpr uint8_t kModGui = 0x08 | 0x80;    // L/R Cmd / Win / GUI

static const char* kMenuItems[] = {"New file", "New folder", "Rename", "Delete", "Bluetooth", "Orientation",
                                   "Forget HID devices"};
static constexpr int kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);
static const char* kOrientationItems[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
static constexpr int kOrientationCount = sizeof(kOrientationItems) / sizeof(kOrientationItems[0]);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
void showToast(const char* msg) {
  strncpy(g_state.toast, msg, sizeof(g_state.toast) - 1);
  g_state.toast[sizeof(g_state.toast) - 1] = '\0';
  g_state.toastUntil = millis() + 2200;
  uiInvalidate(false);
}

static bool toastActive() { return g_state.toast[0] && millis() < g_state.toastUntil; }

static void joinPath(char* out, size_t outsz, const char* base, const char* leaf) {
  if (strcmp(base, "/") == 0) {
    snprintf(out, outsz, "/%s", leaf);
  } else {
    snprintf(out, outsz, "%s/%s", base, leaf);
  }
}

// Display label -> raw name (strip a trailing '/').
static void rawName(char* out, size_t outsz, const char* label) {
  strncpy(out, label, outsz - 1);
  out[outsz - 1] = '\0';
  size_t n = strlen(out);
  if (n > 0 && out[n - 1] == '/') out[n - 1] = '\0';
}

static bool hasMarkdownExt(const char* name) {
  const size_t n = strlen(name);
  auto ends = [&](const char* ext) {
    const size_t e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
  };
  return ends(".md") || ends(".markdown") || ends(".txt");
}

static int moveSel(int sel, int delta, int count) {
  if (count <= 0) return 0;
  int s = (sel + delta) % count;
  if (s < 0) s += count;
  return s;
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------
static void sortEntries(int start) {
  AppState& st = g_state;
  for (int i = start + 1; i < st.entryCount; ++i) {
    char tmpName[64];
    strncpy(tmpName, st.names[i], sizeof(tmpName));
    bool tmpDir = st.isDir[i];
    int j = i - 1;
    auto less = [&](bool ad, const char* a, bool bd, const char* b) {
      if (ad != bd) return ad;  // folders first
      return strcasecmp(a, b) < 0;
    };
    while (j >= start && less(tmpDir, tmpName, st.isDir[j], st.names[j])) {
      strncpy(st.names[j + 1], st.names[j], sizeof(st.names[j + 1]));
      st.isDir[j + 1] = st.isDir[j];
      --j;
    }
    strncpy(st.names[j + 1], tmpName, sizeof(st.names[j + 1]));
    st.isDir[j + 1] = tmpDir;
  }
}

void loadDir() {
  AppState& st = g_state;
  st.entryCount = 0;
  st.browserSel = 0;
  st.browserTop = 0;

  const bool root = strcmp(st.path, "/") == 0;
  if (!root) {
    strncpy(st.names[0], "..", sizeof(st.names[0]));
    st.isDir[0] = true;
    st.entryCount = 1;
  }

  FsFile dir = SdMan.open(st.path, O_RDONLY);
  if (dir && dir.isDirectory()) {
    char nm[64];
    for (FsFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
      const bool isdir = f.isDirectory();
      f.getName(nm, sizeof(nm));
      f.close();
      if (nm[0] == '.') continue;
      if (!isdir && !hasMarkdownExt(nm)) continue;
      if (st.entryCount >= AppState::kMaxEntries) break;
      const int i = st.entryCount++;
      st.isDir[i] = isdir;
      if (isdir) {
        snprintf(st.names[i], sizeof(st.names[i]), "%s/", nm);
      } else {
        strncpy(st.names[i], nm, sizeof(st.names[i]) - 1);
        st.names[i][sizeof(st.names[i]) - 1] = '\0';
      }
    }
    dir.close();
  }

  sortEntries(root ? 0 : 1);

  for (int i = 0; i < st.entryCount; ++i) {
    st.items[i] = ListItem{};
    st.items[i].label = st.names[i];
    st.items[i].actionValue = static_cast<int16_t>(i);
  }
}

static void goUp() {
  AppState& st = g_state;
  if (strcmp(st.path, "/") == 0) return;
  char* slash = strrchr(st.path, '/');
  if (!slash || slash == st.path) {
    strcpy(st.path, "/");
  } else {
    *slash = '\0';
  }
  loadDir();
  uiInvalidate(true);
}

// ---------------------------------------------------------------------------
// Browser actions
// ---------------------------------------------------------------------------
static void browserOpen() {
  AppState& st = g_state;
  if (st.entryCount == 0) return;
  const int i = st.browserSel;
  const char* label = st.names[i];

  if (st.isDir[i]) {
    if (strcmp(label, "..") == 0) {
      goUp();
      return;
    }
    char leaf[64];
    rawName(leaf, sizeof(leaf), label);
    char next[160];
    joinPath(next, sizeof(next), st.path, leaf);
    strncpy(st.path, next, sizeof(st.path) - 1);
    st.path[sizeof(st.path) - 1] = '\0';
    loadDir();
    uiInvalidate(true);
    return;
  }

  char full[224];
  joinPath(full, sizeof(full), st.path, label);
  if (g_state.doc.load(full)) {
    if (g_state.doc.truncated()) showToast("File too large; truncated");
    uiGotoScreen(AppScreen::Editor);
  } else {
    showToast("Open failed");
  }
}

static void openMenu() {
  AppState& st = g_state;
  st.menuTarget = -1;
  if (st.entryCount > 0 && strcmp(st.names[st.browserSel], "..") != 0) st.menuTarget = st.browserSel;
  st.menuSel = 0;
  uiGotoScreen(AppScreen::Menu);
}

// ---------------------------------------------------------------------------
// Name entry / file ops
// ---------------------------------------------------------------------------
static void startNameEntry(NamePurpose purpose, const char* prefill) {
  if (!BleHid.isConnected()) {
    showToast("Connect a keyboard first");
    return;
  }
  AppState& st = g_state;
  st.namePurpose = purpose;
  st.nameBuf[0] = '\0';
  st.nameLen = 0;
  st.nameCursor = 0;
  if (prefill) {
    strncpy(st.nameBuf, prefill, sizeof(st.nameBuf) - 1);
    st.nameBuf[sizeof(st.nameBuf) - 1] = '\0';
    st.nameLen = strlen(st.nameBuf);
    st.nameCursor = st.nameLen;
  }
  uiGotoScreen(AppScreen::NameEntry);
}

static void startRename() {
  AppState& st = g_state;
  if (st.menuTarget < 0) {
    showToast("Select a file first");
    return;
  }
  char raw[64];
  rawName(raw, sizeof(raw), st.names[st.menuTarget]);
  startNameEntry(NamePurpose::Rename, raw);
}

static void startDelete() {
  AppState& st = g_state;
  if (st.menuTarget < 0) {
    showToast("Select a file first");
    return;
  }
  uiGotoScreen(AppScreen::ConfirmDelete);
}

static void ensureMdExt(char* name, size_t sz) {
  if (!hasMarkdownExt(name)) {
    strncat(name, ".md", sz - strlen(name) - 1);
  }
}

static void commitName() {
  AppState& st = g_state;
  if (st.nameLen == 0) {
    showToast("Name required");
    return;
  }

  if (st.namePurpose == NamePurpose::NewFile) {
    char leaf[80];
    strncpy(leaf, st.nameBuf, sizeof(leaf) - 1);
    leaf[sizeof(leaf) - 1] = '\0';
    ensureMdExt(leaf, sizeof(leaf));
    char full[224];
    joinPath(full, sizeof(full), st.path, leaf);
    if (SdMan.exists(full)) {
      showToast("Already exists");
      return;
    }
    g_state.doc.reset(full);
    if (!g_state.doc.save()) {
      showToast("Create failed");
      return;
    }
    loadDir();
    uiGotoScreen(AppScreen::Editor);
    return;
  }

  if (st.namePurpose == NamePurpose::NewFolder) {
    char full[224];
    joinPath(full, sizeof(full), st.path, st.nameBuf);
    if (SdMan.exists(full)) {
      showToast("Already exists");
      return;
    }
    if (!SdMan.mkdir(full)) {
      showToast("mkdir failed");
      return;
    }
    loadDir();
    uiGotoScreen(AppScreen::Browser);
    return;
  }

  // Rename
  if (st.menuTarget < 0) {
    uiGotoScreen(AppScreen::Browser);
    return;
  }
  char oldraw[64];
  rawName(oldraw, sizeof(oldraw), st.names[st.menuTarget]);
  char oldfull[224];
  joinPath(oldfull, sizeof(oldfull), st.path, oldraw);

  char leaf[80];
  strncpy(leaf, st.nameBuf, sizeof(leaf) - 1);
  leaf[sizeof(leaf) - 1] = '\0';
  if (!st.isDir[st.menuTarget]) ensureMdExt(leaf, sizeof(leaf));
  char newfull[224];
  joinPath(newfull, sizeof(newfull), st.path, leaf);

  if (!SdMan.rename(oldfull, newfull)) {
    showToast("Rename failed");
    return;
  }
  loadDir();
  uiGotoScreen(AppScreen::Browser);
}

static void doDelete() {
  AppState& st = g_state;
  if (st.menuTarget < 0) {
    uiGotoScreen(AppScreen::Browser);
    return;
  }
  char raw[64];
  rawName(raw, sizeof(raw), st.names[st.menuTarget]);
  char full[224];
  joinPath(full, sizeof(full), st.path, raw);
  bool ok = st.isDir[st.menuTarget] ? SdMan.removeDir(full) : SdMan.remove(full);
  if (!ok) showToast("Delete failed");
  loadDir();
  uiGotoScreen(AppScreen::Browser);
}

// ---------------------------------------------------------------------------
// Bluetooth
// ---------------------------------------------------------------------------
static constexpr int kProbeBleCandidateRssi = -90;
static constexpr uint8_t kProbeBleCandidateRows = 10;

static void enterBluetooth() {
  g_state.btSel = 0;
  g_state.btTop = 0;
  BleHid.startScan(60000);
  uiGotoScreen(AppScreen::Bluetooth);
}

static void enterOrientation() {
  g_state.orientationSel = g_settings.orientation;
  uiGotoScreen(AppScreen::Orientation);
}

static void selectOrientation() {
  g_settings.orientation = static_cast<uint8_t>(g_state.orientationSel);
  g_settings.save();
  uiApplyOrientation();
  showToast(g_settings.orientationLabel());
}

static void forgetAll() {
  while (BleHid.pairedCount() > 0) BleHid.forget(BleHid.paired(0).addr);
  showToast("Forgot HID devices");
}

// Rebuild the bluetooth row list from live host state (called each render).
static void buildBtRows() {
  AppState& st = g_state;
  uint8_t r = 0;
  // Row 0: scan/rescan.
  strncpy(st.btLabel[r], BleHid.isScanning() ? "Scanning..." : "Scan for HID devices", sizeof(st.btLabel[r]) - 1);
  st.btLabel[r][sizeof(st.btLabel[r]) - 1] = '\0';
  st.btAddr[r][0] = '\0';
  st.btKind[r] = 0;
  ++r;

  // Paired HID peripherals.
  for (uint8_t i = 0; i < BleHid.pairedCount() && r < AppState::kBtRows; ++i) {
    const freeink::PairedHidDevice& p = BleHid.paired(i);
    const bool live = BleHid.isConnected() && strcmp(BleHid.connectedName(), p.name) == 0;
    snprintf(st.btLabel[r], sizeof(st.btLabel[r]), "%s %s", p.name[0] ? p.name : p.addr,
             live ? "[connected]" : "[paired]");
    strncpy(st.btAddr[r], p.addr, sizeof(st.btAddr[r]) - 1);
    st.btAddr[r][sizeof(st.btAddr[r]) - 1] = '\0';
    st.btKind[r] = 1;
    ++r;
  }

  // Newly discovered, ranked so HID devices float to the top: HID devices first,
  // then named devices, then anonymous — each group strongest-signal first. Many
  // BLE devices only reveal their name in a scan response the C3 may not catch,
  // but the HID service often rides the primary advertisement, so an input
  // peripheral may still be tagged [HID] even when it shows by address.
  uint8_t order[freeink::BleKeyboardHost::kMaxDiscovered];
  uint8_t n = 0;
  for (uint8_t i = 0; i < BleHid.deviceCount(); ++i) {
    const freeink::DiscoveredDevice& d = BleHid.device(i);
    bool dup = false;
    for (uint8_t k = 0; k < BleHid.pairedCount(); ++k) {
      if (strcmp(BleHid.paired(k).addr, d.addr) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup) order[n++] = i;
  }
  auto rankOf = [](const freeink::DiscoveredDevice& d) -> int {
    if (d.hid) return 2;
    return d.hasName ? 1 : 0;
  };
  for (uint8_t a = 0; a < n; ++a) {
    for (uint8_t b = static_cast<uint8_t>(a + 1); b < n; ++b) {
      const freeink::DiscoveredDevice& da = BleHid.device(order[a]);
      const freeink::DiscoveredDevice& db = BleHid.device(order[b]);
      const int ra = rankOf(da), rb = rankOf(db);
      if (rb > ra || (rb == ra && db.rssi > da.rssi)) {
        const uint8_t t = order[a];
        order[a] = order[b];
        order[b] = t;
      }
    }
  }
  bool hasHidCandidate = false;
  for (uint8_t j = 0; j < n && r < AppState::kBtRows; ++j) {
    const freeink::DiscoveredDevice& d = BleHid.device(order[j]);
    // Keep the HID picker focused: show named devices and devices that
    // advertise HID. Anonymous connectable devices are handled below only as a
    // fallback when the scan found nothing identifiable.
    const bool identifiable = d.hid || d.hasName;
    if (!identifiable) continue;
    if (d.hid) hasHidCandidate = true;
    snprintf(st.btLabel[r], sizeof(st.btLabel[r]), "%s%s  %ddBm", d.hid ? "[HID] " : "",
             d.name[0] ? d.name : d.addr, d.rssi);
    strncpy(st.btAddr[r], d.addr, sizeof(st.btAddr[r]) - 1);
    st.btAddr[r][sizeof(st.btAddr[r]) - 1] = '\0';
    st.btKind[r] = 1;
    ++r;
  }
  if (!hasHidCandidate) {
    uint8_t probeRows = 0;
    for (uint8_t j = 0; j < n && r < AppState::kBtRows && probeRows < kProbeBleCandidateRows; ++j) {
      const freeink::DiscoveredDevice& d = BleHid.device(order[j]);
      if (!d.connectable || d.rssi < kProbeBleCandidateRssi || d.hid) continue;
      snprintf(st.btLabel[r], sizeof(st.btLabel[r]), "[?] %s  %ddBm", d.hasName ? d.name : d.addr, d.rssi);
      strncpy(st.btAddr[r], d.addr, sizeof(st.btAddr[r]) - 1);
      st.btAddr[r][sizeof(st.btAddr[r]) - 1] = '\0';
      st.btKind[r] = 1;
      ++r;
      ++probeRows;
    }
  }

  st.btRowCount = r;
  if (st.btSel >= st.btRowCount) st.btSel = st.btRowCount > 0 ? st.btRowCount - 1 : 0;
  for (uint8_t i = 0; i < st.btRowCount; ++i) {
    st.btItems[i] = ListItem{};
    st.btItems[i].label = st.btLabel[i];
    st.btItems[i].actionValue = static_cast<int16_t>(i);
  }
}

static void btSelect() {
  AppState& st = g_state;
  if (st.btRowCount == 0) return;
  if (st.btKind[st.btSel] == 0) {
    BleHid.startScan(60000);
    uiInvalidate(false);
    return;
  }
  if (BleHid.connect(st.btAddr[st.btSel])) {
    showToast("Connecting...");
    BleHid.releaseScanResults();
    uiGotoScreen(AppScreen::Browser);
  }
}

// ---------------------------------------------------------------------------
// Editor vertical navigation (visual-line aware, reusing the textArea wrap)
// ---------------------------------------------------------------------------
static int16_t measurePrefixX(const char* text, uint32_t start, uint32_t count) {
  char tmp[256];
  uint32_t n = count < sizeof(tmp) - 1 ? count : sizeof(tmp) - 1;
  for (uint32_t i = 0; i < n; ++i) tmp[i] = text[start + i];
  tmp[n] = '\0';
  return g_target->measureText(g_theme.bodyText.font, tmp, g_theme.bodyText).width;
}

static void editorMoveVertical(int dir, bool extend = false) {
  Document& d = g_state.doc;
  const char* text = d.text();
  const int16_t width = g_state.edBodyW;
  const TextStyle style = g_theme.bodyText;
  const uint32_t cursor = d.cursor();

  uint32_t caretLine = 0, caretStart = 0;
  bool found = false;
  const uint32_t total = textAreaWalk(*g_target, width, text, style, [&](uint32_t idx, const TextAreaLine& ln) {
    if (!found && cursor >= ln.start && cursor <= static_cast<uint32_t>(ln.start + ln.len)) {
      caretLine = idx;
      caretStart = ln.start;
      found = true;
    }
  });
  if (!found) return;
  if (dir < 0 && caretLine == 0) return;
  const uint32_t targetLine = dir < 0 ? caretLine - 1 : caretLine + 1;
  if (targetLine >= total) return;

  const int16_t targetX = measurePrefixX(text, caretStart, cursor - caretStart);

  uint32_t tStart = 0;
  uint16_t tLen = 0;
  bool ft = false;
  textAreaWalk(*g_target, width, text, style, [&](uint32_t idx, const TextAreaLine& ln) {
    if (!ft && idx == targetLine) {
      tStart = ln.start;
      tLen = ln.len;
      ft = true;
    }
  });
  if (!ft) return;

  uint16_t best = 0;
  int32_t bestDelta = INT32_MAX;
  for (uint16_t c = 0; c <= tLen; ++c) {
    const int16_t x = measurePrefixX(text, tStart, c);
    const int32_t delta = abs(static_cast<int>(x) - static_cast<int>(targetX));
    if (delta < bestDelta) {
      bestDelta = delta;
      best = c;
    }
  }
  d.setCursorExtend(tStart + best, extend);
}

static void editorKey(const KeyEvent& ev) {
  Document& d = g_state.doc;
  const bool ctrl = (ev.mods & kModCtrl) != 0;
  const bool shift = (ev.mods & kModShift) != 0;
  const bool alt = (ev.mods & kModAlt) != 0;
  const bool gui = (ev.mods & kModGui) != 0;

  // --- Cmd / Ctrl letter shortcuts -----------------------------------------
  if ((ctrl || gui) && ev.special == SpecialKey::None && ev.ch) {
    char c = ev.ch;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);

    // Line-level markdown + strikethrough (work with Cmd or Ctrl). These use the
    // raw character so the shifted symbols (*, &, >) match the standard combos.
    if (shift && c == 'x') {  // Cmd/Ctrl-Shift-X: strikethrough
      d.wrapSelection("~~");
      uiInvalidate(false);
      return;
    }
    switch (ev.ch) {
      case '1':  // Cmd-1: H1
        d.prefixLines("# ");
        uiInvalidate(false);
        return;
      case '2':  // Cmd-2: H2
        d.prefixLines("## ");
        uiInvalidate(false);
        return;
      case '3':  // Cmd-3: H3
        d.prefixLines("### ");
        uiInvalidate(false);
        return;
      case '*':  // Cmd-Shift-8: bullet list
        d.prefixLines("- ");
        uiInvalidate(false);
        return;
      case '&':  // Cmd-Shift-7: numbered list
        d.prefixLines("1. ");
        uiInvalidate(false);
        return;
      case '>':  // Cmd-Shift-.: blockquote
        d.prefixLines("> ");
        uiInvalidate(false);
        return;
      default:
        break;
    }

    // Save + markdown styling work with EITHER Cmd or Ctrl.
    switch (c) {
      case 's':
        showToast(d.save() ? "Saved" : "Save failed");
        return;
      case 'b':
        d.wrapSelection("**");  // bold
        uiInvalidate(false);
        return;
      case 'i':
        d.wrapSelection("*");  // italic
        uiInvalidate(false);
        return;
      case 'e':
        if (gui) {  // Cmd+E: inline code (Ctrl+E is end-of-line below)
          d.wrapSelection("`");
          uiInvalidate(false);
          return;
        }
        break;
      default:
        break;
    }

    if (gui) {  // macOS-style selection / clipboard
      switch (c) {
        case 'a':
          d.selectAll();
          break;
        case 'c':
          d.copySelection();
          break;
        case 'x':
          d.cutSelection();
          break;
        case 'v':
          d.paste();
          break;
        default:
          return;  // unhandled Cmd combo — ignore
      }
    } else {  // Ctrl: readline/emacs-style
      switch (c) {
        case 'a':
          d.moveHome();  // beginning of line
          break;
        case 'e':
          d.moveEnd();  // end of line
          break;
        case 'k':
          d.deleteToLineEnd();  // kill to end of line
          break;
        case 'u':
          d.deleteToLineStart();  // kill to beginning of line
          break;
        case 'w':
          d.deleteWordLeft();  // delete previous word
          break;
        default:
          return;  // unknown Ctrl combo — ignore, don't type the letter
      }
    }
    uiInvalidate(false);
    return;
  }

  // --- Special keys ---------------------------------------------------------
  switch (ev.special) {
    case SpecialKey::Enter:
      d.insert('\n');
      break;
    case SpecialKey::Backspace:
      if (gui) d.deleteToLineStart();      // Cmd+Backspace: to start of line
      else if (alt) d.deleteWordLeft();    // Option+Backspace: previous word
      else d.backspace();                  // (deletes the selection if any)
      break;
    case SpecialKey::Delete:
      if (gui) d.deleteToLineEnd();        // Cmd/Fn+Delete: to end of line
      else d.del();
      break;
    case SpecialKey::Tab:
      d.insert(' ');
      d.insert(' ');
      break;
    case SpecialKey::Left:
      if (gui) d.moveHome(shift);          // Cmd+Left: beginning of line
      else if (alt) d.moveWordLeft(shift); // Option+Left: previous word
      else if (!shift && d.hasSelection()) d.setCursor(d.selStart());  // collapse left
      else d.moveLeft(shift);
      break;
    case SpecialKey::Right:
      if (gui) d.moveEnd(shift);            // Cmd+Right: end of line
      else if (alt) d.moveWordRight(shift); // Option+Right: next word
      else if (!shift && d.hasSelection()) d.setCursor(d.selEnd());  // collapse right
      else d.moveRight(shift);
      break;
    case SpecialKey::Home:
      if (ctrl || gui) d.moveDocStart(shift);
      else d.moveHome(shift);
      break;
    case SpecialKey::End:
      if (ctrl || gui) d.moveDocEnd(shift);
      else d.moveEnd(shift);
      break;
    case SpecialKey::Up:
      if (gui) d.moveDocStart(shift);       // Cmd+Up: top of document
      else editorMoveVertical(-1, shift);
      break;
    case SpecialKey::Down:
      if (gui) d.moveDocEnd(shift);         // Cmd+Down: end of document
      else editorMoveVertical(1, shift);
      break;
    case SpecialKey::PageUp:
      for (uint16_t i = 0; i < g_state.edVisible; ++i) editorMoveVertical(-1, shift);
      break;
    case SpecialKey::PageDown:
      for (uint16_t i = 0; i < g_state.edVisible; ++i) editorMoveVertical(1, shift);
      break;
    case SpecialKey::Escape:
      if (d.dirty()) {
        uiGotoScreen(AppScreen::ConfirmDiscard);
      } else {
        loadDir();
        uiGotoScreen(AppScreen::Browser);
      }
      return;
    case SpecialKey::None:
      if (ev.ch && !ctrl && !gui) d.insert(ev.ch);  // plain text (not a shortcut)
      break;
    default:
      break;
  }
  uiInvalidate(false);
}

static void nameEntryKey(const KeyEvent& ev) {
  AppState& st = g_state;
  switch (ev.special) {
    case SpecialKey::Enter:
      commitName();
      return;
    case SpecialKey::Escape:
      uiGotoScreen(AppScreen::Menu);
      return;
    case SpecialKey::Backspace:
      if (st.nameCursor > 0) {
        memmove(st.nameBuf + st.nameCursor - 1, st.nameBuf + st.nameCursor, st.nameLen - st.nameCursor);
        --st.nameLen;
        --st.nameCursor;
        st.nameBuf[st.nameLen] = '\0';
      }
      break;
    case SpecialKey::Left:
      if (st.nameCursor > 0) --st.nameCursor;
      break;
    case SpecialKey::Right:
      if (st.nameCursor < st.nameLen) ++st.nameCursor;
      break;
    case SpecialKey::None:
      if (ev.ch && ev.ch != '/' && st.nameLen < sizeof(st.nameBuf) - 1) {
        memmove(st.nameBuf + st.nameCursor + 1, st.nameBuf + st.nameCursor, st.nameLen - st.nameCursor);
        st.nameBuf[st.nameCursor] = ev.ch;
        ++st.nameLen;
        ++st.nameCursor;
        st.nameBuf[st.nameLen] = '\0';
      }
      break;
    default:
      break;
  }
  uiInvalidate(false);
}

// ---------------------------------------------------------------------------
// Generic nav/confirm/back (shared by buttons and keyboard on list screens)
// ---------------------------------------------------------------------------
static void uiNav(int delta) {
  AppState& st = g_state;
  switch (st.screen) {
    case AppScreen::Browser:
      st.browserSel = moveSel(st.browserSel, delta, st.entryCount);
      break;
    case AppScreen::Menu:
      st.menuSel = moveSel(st.menuSel, delta, kMenuCount);
      break;
    case AppScreen::Orientation:
      st.orientationSel = moveSel(st.orientationSel, delta, kOrientationCount);
      break;
    case AppScreen::Bluetooth:
      st.btSel = moveSel(st.btSel, delta, st.btRowCount);
      break;
    case AppScreen::Editor:
      editorMoveVertical(delta);
      break;
    default:
      return;
  }
  uiInvalidate(false);
}

static void uiConfirm() {
  switch (g_state.screen) {
    case AppScreen::Browser:
      browserOpen();
      break;
    case AppScreen::Editor:
      showToast(g_state.doc.save() ? "Saved" : "Save failed");
      break;
    case AppScreen::Menu:
      switch (g_state.menuSel) {
        case 0:
          startNameEntry(NamePurpose::NewFile, nullptr);
          break;
        case 1:
          startNameEntry(NamePurpose::NewFolder, nullptr);
          break;
        case 2:
          startRename();
          break;
        case 3:
          startDelete();
          break;
        case 4:
          enterBluetooth();
          break;
        case 5:
          enterOrientation();
          break;
        case 6:
          forgetAll();
          break;
      }
      break;
    case AppScreen::NameEntry:
      commitName();
      break;
    case AppScreen::Bluetooth:
      btSelect();
      break;
    case AppScreen::Orientation:
      selectOrientation();
      break;
    case AppScreen::ConfirmDelete:
      doDelete();
      break;
    case AppScreen::ConfirmDiscard:
      loadDir();
      uiGotoScreen(AppScreen::Browser);
      break;
  }
}

static void uiBack() {
  switch (g_state.screen) {
    case AppScreen::Browser:
      openMenu();
      break;
    case AppScreen::Editor:
      if (g_state.doc.dirty()) {
        uiGotoScreen(AppScreen::ConfirmDiscard);
      } else {
        loadDir();
        uiGotoScreen(AppScreen::Browser);
      }
      break;
    case AppScreen::Menu:
      uiGotoScreen(AppScreen::Browser);
      break;
    case AppScreen::Orientation:
      uiGotoScreen(AppScreen::Menu);
      break;
    case AppScreen::NameEntry:
      uiGotoScreen(AppScreen::Menu);
      break;
    case AppScreen::Bluetooth:
      BleHid.stopScan();
      uiGotoScreen(AppScreen::Browser);
      break;
    case AppScreen::Boot:
    case AppScreen::Sleep:
      break;
    case AppScreen::ConfirmDelete:
      uiGotoScreen(AppScreen::Browser);
      break;
    case AppScreen::ConfirmDiscard:
      uiGotoScreen(AppScreen::Editor);
      break;
  }
}

// ---------------------------------------------------------------------------
// Input entry points
// ---------------------------------------------------------------------------
void handleKey(const KeyEvent& ev) {
  if (g_state.screen == AppScreen::Editor) {
    editorKey(ev);
    return;
  }
  if (g_state.screen == AppScreen::NameEntry) {
    nameEntryKey(ev);
    return;
  }
  switch (ev.special) {
    case SpecialKey::Up:
      uiNav(-1);
      break;
    case SpecialKey::Down:
      uiNav(1);
      break;
    case SpecialKey::Enter:
      uiConfirm();
      break;
    case SpecialKey::Escape:
    case SpecialKey::Backspace:
      uiBack();
      break;
    default:
      break;
  }
}

void handleButton(uint8_t button) {
  // In the editor, Left/Right move the caret horizontally; elsewhere they act as
  // list navigation (alongside Up/Down).
  switch (button) {
    case InputManager::BTN_UP:
      uiNav(-1);
      break;
    case InputManager::BTN_DOWN:
      uiNav(1);
      break;
    case InputManager::BTN_LEFT:
      if (g_state.screen == AppScreen::Editor) {
        g_state.doc.moveLeft();
        uiInvalidate(false);
      } else {
        uiNav(-1);
      }
      break;
    case InputManager::BTN_RIGHT:
      if (g_state.screen == AppScreen::Editor) {
        g_state.doc.moveRight();
        uiInvalidate(false);
      } else {
        uiNav(1);
      }
      break;
    case InputManager::BTN_CONFIRM:
      uiConfirm();
      break;
    case InputManager::BTN_BACK:
      uiBack();
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Status label
// ---------------------------------------------------------------------------
void refreshStatusLabel() {
  if (BleHid.isConnected()) {
    snprintf(g_state.statusLabel, sizeof(g_state.statusLabel), "* %s", BleHid.connectedName());
  } else if (BleHid.pairedCount() > 0) {
    strncpy(g_state.statusLabel, "Reconnecting...", sizeof(g_state.statusLabel) - 1);
    g_state.statusLabel[sizeof(g_state.statusLabel) - 1] = '\0';
  } else {
    strncpy(g_state.statusLabel, "No HID input", sizeof(g_state.statusLabel) - 1);
    g_state.statusLabel[sizeof(g_state.statusLabel) - 1] = '\0';
  }
}

// ---------------------------------------------------------------------------
// Footer helper
// ---------------------------------------------------------------------------
static void footer4(ScreenT& s, const char* a, const char* b, const char* c, const char* d) {
  uiDrawHardwareFooter(a, b, c, d);
}

// ---------------------------------------------------------------------------
// Screen render functions
// ---------------------------------------------------------------------------
void browserScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  s.header(st.path, nullptr, st.statusLabel);
  footer4(s, "Menu", "Open", "Up", "Down");

  if (st.entryCount == 0) {
    s.popup("Empty folder");
  } else {
    Rect body = s.body();
    st.browserVisible = listVisibleRows(body, g_theme.rowHeight);
    st.browserTop = listTopIndexFor(st.browserSel, st.browserTop, st.browserVisible, st.entryCount);
    ListProps p{};
    p.items = st.items;
    p.count = st.entryCount;
    p.selectedIndex = st.browserSel;
    p.topIndex = st.browserTop;
    p.selectionMarker = SelectionMarker::Triangle;
    s.list(p);
  }
  if (toastActive()) s.popup(st.toast);
}

void bootScreen(ScreenT& s, void*) {
  s.setContentMargin(Insets{24, 24, 24, 24});
  Rect body = s.body();
  TextStyle title = g_theme.titleText;
  title.align = TextAlign::Center;
  title.maxLines = 1;
  TextStyle bodyText = g_theme.bodyText;
  bodyText.align = TextAlign::Center;
  bodyText.maxLines = 2;

  Rect titleRect{body.x, static_cast<int16_t>(body.y + body.height / 2 - 42), body.width, 32};
  Rect subRect{body.x, static_cast<int16_t>(titleRect.bottom() + 10), body.width, 44};
  s.frame().target().text(titleRect, "InkDeck", title);
  s.frame().target().text(subRect, "Booting...", bodyText);
}

void editorScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  s.header(st.doc.name(), nullptr, st.statusLabel);
  footer4(s, "Back", "Save", "Up", "Down");

  Rect body = s.body();
  st.edBodyW = body.width;
  st.edLineH = s.frame().target().lineHeight(g_theme.bodyText.font);
  st.edVisible = (st.edLineH > 0 && body.height > 0) ? static_cast<uint16_t>(body.height / st.edLineH) : 1;

  TextAreaMetrics m =
      textAreaMeasure(s.frame().target(), body.width, st.doc.text(), g_theme.bodyText, st.doc.cursor());
  st.doc.setTopLine(textAreaTopLineFor(m.caretLine, st.doc.topLine(), st.edVisible, m.lineCount));

  TextAreaProps ta{};
  ta.text = st.doc.text();
  ta.cursor = st.doc.cursor();
  ta.topLine = st.doc.topLine();
  ta.style = g_theme.bodyText;
  ta.showCaret = true;
  ta.selStart = st.doc.selStart();
  ta.selEnd = st.doc.selEnd();
  s.textArea(ta);

  if (toastActive()) s.popup(st.toast);
}

void menuScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  s.header("Menu", nullptr, st.statusLabel);
  footer4(s, "Back", "Select", "Up", "Down");

  static ListItem items[kMenuCount];
  for (int i = 0; i < kMenuCount; ++i) {
    items[i] = ListItem{};
    items[i].label = kMenuItems[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }
  ListProps p{};
  p.items = items;
  p.count = kMenuCount;
  p.selectedIndex = st.menuSel;
  p.selectionMarker = SelectionMarker::Triangle;
  s.list(p);

  if (toastActive()) s.popup(st.toast);
}

void orientationScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  s.header("Orientation", nullptr, g_settings.orientationLabel());
  footer4(s, "Back", "Select", "Up", "Down");

  static ListItem items[kOrientationCount];
  for (int i = 0; i < kOrientationCount; ++i) {
    items[i] = ListItem{};
    items[i].label = kOrientationItems[i];
    items[i].value = (i == g_settings.orientation) ? "current" : "";
    items[i].actionValue = static_cast<int16_t>(i);
  }
  ListProps p{};
  p.items = items;
  p.count = kOrientationCount;
  p.selectedIndex = st.orientationSel;
  p.selectionMarker = SelectionMarker::Triangle;
  s.list(p);

  if (toastActive()) s.popup(st.toast);
}

void nameEntryScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  const char* title = st.namePurpose == NamePurpose::NewFile     ? "New file name"
                      : st.namePurpose == NamePurpose::NewFolder ? "New folder name"
                                                                 : "Rename";
  s.header(title, nullptr, st.statusLabel);
  footer4(s, "Cancel", "OK", "", "");

  s.spacer(g_theme.spaceLg);
  Rect field = s.takeTop(static_cast<int16_t>(g_theme.rowHeight + 6), g_theme.spaceMd);
  TextFieldProps tf{};
  tf.text = st.nameBuf;
  tf.textStyle = g_theme.bodyText;
  tf.styles = g_theme.textField;  // white field + border; NOT the black "selected" fill
  tf.cursor = st.nameCursor;
  tf.cursorVisible = true;
  tf.selected = false;            // selected resolves to a black background -> black-on-black text
  textField(s.frame(), field, tf);

  TextStyle hint = g_theme.smallText;
  hint.align = TextAlign::Center;
  hint.maxLines = 2;
  s.frame().target().text(s.body(), "Type a name on the keyboard.\nEnter to confirm, Esc to cancel.", hint);

  if (toastActive()) s.popup(st.toast);
}

void bluetoothScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  buildBtRows();
  char title[40];
  snprintf(title, sizeof(title), "Bluetooth  (%u paired)", BleHid.pairedCount());
  s.header(title, nullptr, st.statusLabel);
  footer4(s, "Back", "Select", "Up", "Down");

  Rect body = s.body();
  st.btVisible = listVisibleRows(body, g_theme.rowHeight);
  st.btTop = listTopIndexFor(st.btSel, st.btTop, st.btVisible, st.btRowCount);
  ListProps p{};
  p.items = st.btItems;
  p.count = st.btRowCount;
  p.selectedIndex = st.btSel;
  p.topIndex = st.btTop;
  p.selectionMarker = SelectionMarker::Triangle;
  s.list(p);

  if (toastActive()) s.popup(st.toast);
}

void sleepScreen(ScreenT& s, void*) {
  s.setContentMargin(Insets{24, 24, 24, 24});
  Rect body = s.body();
  TextStyle title = g_theme.titleText;
  title.align = TextAlign::Center;
  title.maxLines = 1;
  TextStyle bodyText = g_theme.bodyText;
  bodyText.align = TextAlign::Center;
  bodyText.maxLines = 3;

  Rect titleRect{body.x, static_cast<int16_t>(body.y + body.height / 2 - 54), body.width, 32};
  Rect subRect{body.x, static_cast<int16_t>(titleRect.bottom() + 10), body.width, 72};
  s.frame().target().text(titleRect, "InkDeck", title);
  s.frame().target().text(subRect, "Sleeping\nHold power to wake", bodyText);
}

static void confirmScreen(ScreenT& s, const char* title, const char* body, const char* okLabel) {
  s.header(title, nullptr, g_state.statusLabel);
  footer4(s, "Cancel", okLabel, "", "");
  TextStyle ts = g_theme.bodyText;
  ts.align = TextAlign::Center;
  ts.maxLines = 5;
  s.frame().target().text(s.body(), body, ts);
}

void confirmDeleteScreen(ScreenT& s, void*) {
  AppState& st = g_state;
  char msg[128];
  char raw[64];
  if (st.menuTarget >= 0) {
    rawName(raw, sizeof(raw), st.names[st.menuTarget]);
  } else {
    raw[0] = '\0';
  }
  snprintf(msg, sizeof(msg), "Delete \"%s\"?\n%s", raw,
           (st.menuTarget >= 0 && st.isDir[st.menuTarget]) ? "(folder and everything in it)" : "");
  confirmScreen(s, "Confirm delete", msg, "Delete");
}

void confirmDiscardScreen(ScreenT& s, void*) {
  confirmScreen(s, "Unsaved changes", "Discard changes to this file?\n(Ctrl-S or Save keeps them.)", "Discard");
}
