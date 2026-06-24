#include "Document.h"

#include <SDCardManager.h>
#include <string.h>

char Document::clip_[INKDECK_CLIP_CAPACITY] = {0};
uint32_t Document::clipLen_ = 0;

namespace {
bool isWordChar(char c) { return c != ' ' && c != '\n' && c != '\t'; }
}  // namespace

void Document::reset(const char* path) {
  len_ = 0;
  cursor_ = 0;
  anchor_ = 0;
  topLine_ = 0;
  dirty_ = false;
  truncated_ = false;
  buf_[0] = '\0';
  path_[0] = '\0';
  name_[0] = '\0';
  if (path) {
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    const char* slash = strrchr(path_, '/');
    const char* base = slash ? slash + 1 : path_;
    strncpy(name_, base, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
  }
}

bool Document::load(const char* path) {
  reset(path);
  FsFile f = SdMan.open(path, O_RDONLY);
  if (!f) return false;
  uint32_t n = 0;
  const uint32_t cap = INKDECK_DOC_CAPACITY - 1;
  while (n < cap) {
    uint32_t want = cap - n;
    if (want > 512) want = 512;
    int r = f.read(reinterpret_cast<uint8_t*>(buf_) + n, want);
    if (r <= 0) break;
    n += static_cast<uint32_t>(r);
  }
  truncated_ = f.available() > 0;
  f.close();
  len_ = n;
  buf_[len_] = '\0';
  cursor_ = len_;  // open with the caret at the end of the existing text
  anchor_ = cursor_;
  topLine_ = 0;
  dirty_ = false;
  return true;
}

bool Document::save() {
  if (!path_[0]) return false;
  FsFile f = SdMan.open(path_, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  uint32_t w = len_ > 0 ? f.write(reinterpret_cast<const uint8_t*>(buf_), len_) : 0;
  f.close();
  if (w != len_) return false;
  dirty_ = false;
  return true;
}

// ---------------------------------------------------------------------------
// Low-level buffer ops
// ---------------------------------------------------------------------------
void Document::deleteRange(uint32_t from, uint32_t to) {
  if (from >= to || to > len_) return;
  memmove(buf_ + from, buf_ + to, len_ - to);
  len_ -= (to - from);
  buf_[len_] = '\0';
  cursor_ = from;
  anchor_ = from;
  dirty_ = true;
}

void Document::deleteRaw(uint32_t from, uint32_t to) {
  if (from >= to || to > len_) return;
  memmove(buf_ + from, buf_ + to, len_ - to);
  len_ -= (to - from);
  buf_[len_] = '\0';
}

void Document::deleteSelection() {
  if (hasSelection()) deleteRange(selStart(), selEnd());
}

void Document::insertTextAt(uint32_t pos, const char* s, uint32_t n) {
  if (n == 0 || len_ + n > INKDECK_DOC_CAPACITY - 1) return;
  if (pos > len_) pos = len_;
  memmove(buf_ + pos + n, buf_ + pos, len_ - pos);
  memcpy(buf_ + pos, s, n);
  len_ += n;
  buf_[len_] = '\0';
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Editing (replace the selection if there is one)
// ---------------------------------------------------------------------------
void Document::insert(char c) {
  if (hasSelection()) deleteSelection();
  if (len_ >= INKDECK_DOC_CAPACITY - 1) return;
  memmove(buf_ + cursor_ + 1, buf_ + cursor_, len_ - cursor_);
  buf_[cursor_] = c;
  ++len_;
  ++cursor_;
  anchor_ = cursor_;
  buf_[len_] = '\0';
  dirty_ = true;
}

void Document::backspace() {
  if (hasSelection()) {
    deleteSelection();
    return;
  }
  if (cursor_ == 0) return;
  memmove(buf_ + cursor_ - 1, buf_ + cursor_, len_ - cursor_);
  --len_;
  --cursor_;
  anchor_ = cursor_;
  buf_[len_] = '\0';
  dirty_ = true;
}

void Document::del() {
  if (hasSelection()) {
    deleteSelection();
    return;
  }
  if (cursor_ >= len_) return;
  memmove(buf_ + cursor_, buf_ + cursor_ + 1, len_ - cursor_ - 1);
  --len_;
  buf_[len_] = '\0';
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Movement (extend grows the selection from the anchor)
// ---------------------------------------------------------------------------
void Document::setCursorExtend(uint32_t c, bool extend) {
  cursor_ = c > len_ ? len_ : c;
  if (!extend) anchor_ = cursor_;
}

void Document::moveLeft(bool extend) {
  if (cursor_ > 0) --cursor_;
  if (!extend) anchor_ = cursor_;
}

void Document::moveRight(bool extend) {
  if (cursor_ < len_) ++cursor_;
  if (!extend) anchor_ = cursor_;
}

void Document::moveHome(bool extend) {
  while (cursor_ > 0 && buf_[cursor_ - 1] != '\n') --cursor_;
  if (!extend) anchor_ = cursor_;
}

void Document::moveEnd(bool extend) {
  while (cursor_ < len_ && buf_[cursor_] != '\n') ++cursor_;
  if (!extend) anchor_ = cursor_;
}

void Document::moveDocStart(bool extend) {
  cursor_ = 0;
  if (!extend) anchor_ = cursor_;
}

void Document::moveDocEnd(bool extend) {
  cursor_ = len_;
  if (!extend) anchor_ = cursor_;
}

void Document::moveWordLeft(bool extend) {
  while (cursor_ > 0 && !isWordChar(buf_[cursor_ - 1])) --cursor_;  // skip separators
  while (cursor_ > 0 && isWordChar(buf_[cursor_ - 1])) --cursor_;   // skip the word
  if (!extend) anchor_ = cursor_;
}

void Document::moveWordRight(bool extend) {
  while (cursor_ < len_ && !isWordChar(buf_[cursor_])) ++cursor_;
  while (cursor_ < len_ && isWordChar(buf_[cursor_])) ++cursor_;
  if (!extend) anchor_ = cursor_;
}

void Document::deleteToLineStart() {
  uint32_t start = cursor_;
  while (start > 0 && buf_[start - 1] != '\n') --start;
  deleteRange(start, cursor_);
}

void Document::deleteToLineEnd() {
  uint32_t end = cursor_;
  while (end < len_ && buf_[end] != '\n') ++end;
  deleteRange(cursor_, end);
}

void Document::deleteWordLeft() {
  uint32_t start = cursor_;
  while (start > 0 && !isWordChar(buf_[start - 1])) --start;
  while (start > 0 && isWordChar(buf_[start - 1])) --start;
  deleteRange(start, cursor_);
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------
void Document::copySelection() {
  if (!hasSelection()) return;
  uint32_t n = selEnd() - selStart();
  if (n > INKDECK_CLIP_CAPACITY - 1) n = INKDECK_CLIP_CAPACITY - 1;
  memcpy(clip_, buf_ + selStart(), n);
  clip_[n] = '\0';
  clipLen_ = n;
}

void Document::cutSelection() {
  if (!hasSelection()) return;
  copySelection();
  deleteSelection();
}

void Document::paste() {
  if (clipLen_ == 0) return;
  if (hasSelection()) deleteSelection();
  const uint32_t before = len_;
  insertTextAt(cursor_, clip_, clipLen_);
  cursor_ += (len_ - before);
  anchor_ = cursor_;
}

// ---------------------------------------------------------------------------
// Markdown styling
// ---------------------------------------------------------------------------
void Document::wrapSelection(const char* marker) {
  const uint32_t m = static_cast<uint32_t>(strlen(marker));
  if (m == 0) return;
  auto matchAt = [&](uint32_t at) -> bool { return at + m <= len_ && memcmp(buf_ + at, marker, m) == 0; };

  if (hasSelection()) {
    const uint32_t s = selStart();
    const uint32_t e = selEnd();
    // Toggle off: markers sit just OUTSIDE the selection.
    if (s >= m && matchAt(s - m) && matchAt(e)) {
      deleteRaw(e, e + m);  // closing
      deleteRaw(s - m, s);  // opening
      anchor_ = s - m;
      cursor_ = e - m;
      dirty_ = true;
      return;
    }
    // Toggle off: markers are INSIDE the selection.
    if (e - s >= 2 * m && matchAt(s) && matchAt(e - m)) {
      deleteRaw(e - m, e);
      deleteRaw(s, s + m);
      anchor_ = s;
      cursor_ = e - 2 * m;
      dirty_ = true;
      return;
    }
    // Otherwise wrap.
    if (len_ + 2 * m > INKDECK_DOC_CAPACITY - 1) return;
    insertTextAt(e, marker, m);
    insertTextAt(s, marker, m);
    anchor_ = s + m;
    cursor_ = e + m;
  } else {
    // No selection: toggle an empty pair at the caret, else insert one to type in.
    if (cursor_ >= m && matchAt(cursor_ - m) && matchAt(cursor_)) {
      deleteRaw(cursor_, cursor_ + m);
      deleteRaw(cursor_ - m, cursor_);
      cursor_ -= m;
      anchor_ = cursor_;
      dirty_ = true;
      return;
    }
    if (len_ + 2 * m > INKDECK_DOC_CAPACITY - 1) return;
    insertTextAt(cursor_, marker, m);
    insertTextAt(cursor_ + m, marker, m);
    cursor_ += m;
    anchor_ = cursor_;
  }
  dirty_ = true;
}

void Document::prefixLines(const char* prefix) {
  const uint32_t p = static_cast<uint32_t>(strlen(prefix));
  if (p == 0) return;
  const bool isHeading = prefix[0] == '#';
  const uint32_t selS = selStart();
  const uint32_t selE = selEnd();

  // Start of the first affected line.
  uint32_t ls = selS;
  while (ls > 0 && buf_[ls - 1] != '\n') --ls;

  // Collect the line-start offsets in [ls, selE].
  uint32_t starts[128];
  uint32_t ns = 0;
  uint32_t pos = ls;
  while (ns < 128) {
    starts[ns++] = pos;
    while (pos < len_ && buf_[pos] != '\n') ++pos;
    if (pos >= len_) break;
    ++pos;  // step past the newline
    if (pos > selE) break;
  }

  // Length of an existing "#... " heading marker at `at` (0 if none).
  auto headingLen = [&](uint32_t at) -> uint32_t {
    uint32_t i = at;
    while (i < len_ && buf_[i] == '#') ++i;
    return (i > at && i < len_ && buf_[i] == ' ') ? (i - at) + 1 : 0;
  };
  auto startsWith = [&](uint32_t at) -> bool { return at + p <= len_ && memcmp(buf_ + at, prefix, p) == 0; };

  // Toggle direction is decided by the first line: if it already carries this
  // exact prefix, REMOVE it from every line; otherwise ADD (headings replace a
  // different existing level).
  const bool remove = isHeading ? (headingLen(starts[0]) == p && startsWith(starts[0])) : startsWith(starts[0]);

  // Per-line edit deltas (computed against the current, pre-edit content).
  int32_t delta[128];
  int32_t net = 0;
  for (uint32_t i = 0; i < ns; ++i) {
    const uint32_t at = starts[i];
    uint32_t rem = 0, add = 0;
    if (remove) {
      if (startsWith(at)) rem = p;
    } else if (isHeading) {
      rem = headingLen(at);  // replace any existing heading level
      add = p;
    } else if (!startsWith(at)) {
      add = p;  // add only where missing
    }
    delta[i] = static_cast<int32_t>(add) - static_cast<int32_t>(rem);
    net += delta[i];
  }
  if (net > 0 && len_ + static_cast<uint32_t>(net) > INKDECK_DOC_CAPACITY - 1) return;

  // Shift caret/anchor by the deltas of line-starts at or before them.
  int32_t curShift = 0, anchShift = 0;
  for (uint32_t i = 0; i < ns; ++i) {
    if (starts[i] <= cursor_) curShift += delta[i];
    if (starts[i] <= anchor_) anchShift += delta[i];
  }

  // Apply from the last line backward so earlier offsets stay valid.
  for (uint32_t i = ns; i-- > 0;) {
    const uint32_t at = starts[i];
    if (remove) {
      if (startsWith(at)) deleteRaw(at, at + p);
    } else if (isHeading) {
      const uint32_t h = headingLen(at);
      if (h) deleteRaw(at, at + h);
      insertTextAt(at, prefix, p);
    } else if (!startsWith(at)) {
      insertTextAt(at, prefix, p);
    }
  }

  cursor_ = curShift < 0 && static_cast<uint32_t>(-curShift) > cursor_ ? 0 : cursor_ + curShift;
  anchor_ = anchShift < 0 && static_cast<uint32_t>(-anchShift) > anchor_ ? 0 : anchor_ + anchShift;
  if (cursor_ > len_) cursor_ = len_;
  if (anchor_ > len_) anchor_ = len_;
  dirty_ = true;
}
