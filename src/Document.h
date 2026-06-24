#pragma once

// The open markdown file: a single fixed-capacity in-RAM text buffer with a
// cursor. Most memory-efficient editor model on the C3 — one static allocation,
// no heap growth or fragmentation while typing. Files larger than the capacity
// load truncated (truncated() is then true). Plain bytes; UTF-8 passes through.

#include <Arduino.h>

#ifndef INKDECK_DOC_CAPACITY
#define INKDECK_DOC_CAPACITY 16384
#endif
#ifndef INKDECK_CLIP_CAPACITY
#define INKDECK_CLIP_CAPACITY 4096
#endif

class Document {
 public:
  // Point the document at `path` with empty contents (no SD I/O).
  void reset(const char* path);
  // Stream the file at `path` into the buffer (truncating to capacity). Returns
  // false if it can't be opened.
  bool load(const char* path);
  // Stream the buffer back to its path. Returns false on failure.
  bool save();

  const char* text() const { return buf_; }  // NUL-terminated
  uint32_t length() const { return len_; }
  uint32_t cursor() const { return cursor_; }
  void setCursor(uint32_t c) {
    cursor_ = c > len_ ? len_ : c;
    anchor_ = cursor_;
  }
  uint32_t topLine() const { return topLine_; }
  void setTopLine(uint32_t t) { topLine_ = t; }
  const char* path() const { return path_; }
  const char* name() const { return name_; }
  bool dirty() const { return dirty_; }
  bool truncated() const { return truncated_; }

  // --- Selection (anchor..cursor) -------------------------------------------
  bool hasSelection() const { return anchor_ != cursor_; }
  uint32_t selStart() const { return anchor_ < cursor_ ? anchor_ : cursor_; }
  uint32_t selEnd() const { return anchor_ > cursor_ ? anchor_ : cursor_; }
  void clearSelection() { anchor_ = cursor_; }  // collapse to caret
  void selectAll() {
    anchor_ = 0;
    cursor_ = len_;
  }

  // --- Editing (operate at the cursor; replace the selection if any) ---------
  void insert(char c);
  void backspace();
  void del();

  // --- Movement (extend=true keeps the anchor to grow the selection) ---------
  void moveLeft(bool extend = false);
  void moveRight(bool extend = false);
  void moveHome(bool extend = false);       // start of the current logical line
  void moveEnd(bool extend = false);        // end of the current logical line
  void moveDocStart(bool extend = false);   // start of the document
  void moveDocEnd(bool extend = false);     // end of the document
  void moveWordLeft(bool extend = false);   // previous word boundary
  void moveWordRight(bool extend = false);  // next word boundary
  void setCursorExtend(uint32_t c, bool extend);  // for vertical moves

  void deleteToLineStart();  // delete from the line start to the caret
  void deleteToLineEnd();    // delete from the caret to the line end
  void deleteWordLeft();     // delete the previous word

  // --- Clipboard -------------------------------------------------------------
  void copySelection();
  void cutSelection();
  void paste();

  // --- Markdown styling: wrap the selection (or the caret) in `marker` -------
  // e.g. "**" bold, "*" italic, "`" code, "~~" strikethrough. With a selection
  // the original text stays selected inside the markers; with none, the caret
  // lands between an empty pair so you can type.
  void wrapSelection(const char* marker);
  // Prefix the current line(s) with `prefix` (e.g. "# ", "- ", "> ").
  void prefixLines(const char* prefix);

 private:
  void deleteRange(uint32_t from, uint32_t to);  // delete [from,to), caret -> from
  void deleteRaw(uint32_t from, uint32_t to);    // delete [from,to), caret untouched
  void deleteSelection();                        // delete + collapse
  void insertTextAt(uint32_t pos, const char* s, uint32_t n);

  char buf_[INKDECK_DOC_CAPACITY];
  uint32_t len_ = 0;
  uint32_t cursor_ = 0;
  uint32_t anchor_ = 0;  // selection anchor (== cursor_ when no selection)
  uint32_t topLine_ = 0;
  char path_[160] = {0};
  char name_[96] = {0};
  bool dirty_ = false;
  bool truncated_ = false;

  static char clip_[INKDECK_CLIP_CAPACITY];
  static uint32_t clipLen_;
};
