#pragma once
#include "Files.h"
#include "Reader.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace inh {
// UTF-8 book envelope: fixed ASCII header, bounded text TOC, then plain UTF-8.
constexpr uint32_t BookHeaderBytes = 46, MaxChapters = 1024,
                   MaxTocBytes = 65536;
constexpr char BookMagic[] = "INHBOOK/1\n";
struct BookLayout {
  uint32_t begin = 0, end = 0, bodyCrc = 0, chapters = 0;
  bool packaged = false;
};
struct Chapter {
  uint32_t offset = 0;
  uint8_t depth = 0;
  char title[193]{};
};
inline bool bookHex(TextSource &s, uint32_t &at, unsigned digits,
                    uint32_t &value) {
  value = 0;
  for (unsigned i = 0; i < digits; ++i) {
    int c = s.byte(at++);
    int n = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                   : -1;
    if (n < 0)
      return false;
    value = (value << 4) | unsigned(n);
  }
  return true;
}
inline bool nextChapter(TextSource &s, const BookLayout &book, uint32_t &at,
                        Chapter &out) {
  uint32_t offset, depth;
  if (at >= book.begin || !bookHex(s, at, 8, offset) || s.byte(at++) != '\t' ||
      !bookHex(s, at, 2, depth) || s.byte(at++) != '\t' || depth > 15 ||
      offset >= book.end - book.begin)
    return false;
  SourceValidator utf;
  size_t n = 0;
  while (at < book.begin) {
    int c = s.byte(at++);
    if (c == '\n') {
      if (!n || !utf.complete())
        return false;
      out.title[n] = 0;
      out.offset = book.begin + offset;
      out.depth = depth;
      return true;
    }
    if (c < 32 || c == 127 || n == 192 || !utf.feed(uint8_t(c)))
      return false;
    out.title[n++] = char(c);
  }
  return false;
}
inline bool parseBook(TextSource &s, BookLayout &out, bool verifyBody = true) {
  out = BookLayout{};
  out.end = s.size();
  for (unsigned i = 0; i < 10; ++i)
    if (i >= s.size() || s.byte(i) != BookMagic[i])
      return true; // legacy TXT
  out.packaged = true;
  if (s.size() < BookHeaderBytes)
    return false;
  uint32_t at = 10, bytes, tableCrc;
  if (!bookHex(s, at, 8, out.begin) || s.byte(at++) != ' ' ||
      !bookHex(s, at, 8, bytes) || s.byte(at++) != ' ' ||
      !bookHex(s, at, 8, out.bodyCrc) || s.byte(at++) != ' ' ||
      !bookHex(s, at, 8, tableCrc) || s.byte(at++) != '\n' ||
      out.begin < BookHeaderBytes ||
      out.begin - BookHeaderBytes > MaxTocBytes || out.begin > s.size() ||
      !bytes || bytes != s.size() - out.begin)
    return false;
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = BookHeaderBytes; i < out.begin; ++i) {
    int c = s.byte(i);
    if (c < 0)
      return false;
    uint8_t b = c;
    crc = crc32(&b, 1, crc);
  }
  if ((crc ^ 0xffffffffu) != tableCrc)
    return false;
  at = BookHeaderBytes;
  uint32_t previous = 0;
  while (at < out.begin) {
    Chapter chapter;
    if (out.chapters == MaxChapters || !nextChapter(s, out, at, chapter) ||
        (out.chapters && chapter.offset < previous))
      return false;
    previous = chapter.offset;
    ++out.chapters;
  }
  if (verifyBody) {
    crc = 0xffffffffu;
    for (uint32_t i = out.begin; i < out.end; ++i) {
      int c = s.byte(i);
      if (c < 0)
        return false;
      uint8_t b = c;
      crc = crc32(&b, 1, crc);
    }
    if ((crc ^ 0xffffffffu) != out.bodyCrc)
      return false;
  }
  return true;
}
inline bool chapterAt(TextSource &s, const BookLayout &book, uint32_t index,
                      Chapter &out) {
  if (index >= book.chapters)
    return false;
  uint32_t at = BookHeaderBytes;
  for (uint32_t i = 0; i <= index; ++i)
    if (!nextChapter(s, book, at, out))
      return false;
  return true;
}
struct ChapterCursor {
  uint32_t cursor = BookHeaderBytes, next = 0;
  bool advance(TextSource &source, const BookLayout &book, uint32_t offset) {
    while (next <= offset && cursor < book.begin) {
      Chapter entry;
      if (!nextChapter(source, book, cursor, entry))
        return false;
      next = entry.offset;
    }
    if (next <= offset)
      next = UINT32_MAX;
    return true;
  }
};
// Upper-bound search reads the on-disk page index without loading it into RAM.
template <class Read>
bool pageForOffset(uint32_t count, uint32_t offset, Read read, uint32_t &page) {
  if (!count)
    return false;
  uint32_t first;
  if (!read(0, first) || offset < first)
    return false;
  uint32_t lo = 0, hi = count;
  while (lo + 1 < hi) {
    uint32_t mid = lo + (hi - lo) / 2, at;
    if (!read(mid, at))
      return false;
    if (at <= offset)
      lo = mid;
    else
      hi = mid;
  }
  page = lo;
  return true;
}
struct ContentsState {
  uint32_t origin = 0, selection = 0, count = 0;
  void enter(uint32_t page, uint32_t chapters, uint32_t selected = 0) {
    origin = page;
    count = chapters;
    selection = chapters ? std::min(selected, chapters - 1) : 0;
  }
  void move(bool previous = false) {
    if (count)
      selection =
          previous ? (selection + count - 1) % count : (selection + 1) % count;
  }
  uint32_t cancel() const { return origin; }
};
} // namespace inh
