#pragma once
#include "Book.h"
#include <array>
namespace inh {
// Old converters preserved chapter headings as ordinary lines of book.txt.
// This fallback never rewrites the book/index and allocates no heap memory.
class PlainContents {
  enum Kind { None, Strong, Dotted, Arabic, Roman };
  struct Match {
    Kind kind = None;
    bool part = false;
    uint8_t levels = 0;
  };
  struct Line {
    uint32_t offset = 0;
    char title[193]{};
    bool valid = true;
  };
  std::array<uint32_t, MaxChapters> starts{};
  bool hasParts = false;
  static bool space(uint8_t c) { return c == ' ' || c == '\t' || c == '\r'; }
  template <class Tick>
  static bool line(TextSource &source, uint32_t &at, Line &out, Tick tick) {
    size_t n = 0;
    bool pendingSpace = false, started = false;
    SourceValidator utf;
    out = Line{};
    while (at < source.size()) {
      uint32_t pos = at;
      int c = source.byte(at++);
      if (c < 0)
        return false;
      if ((at & 4095) == 0)
        tick();
      if (c == '\n')
        break;
      if (space(c)) {
        if (!utf.feed(' '))
          out.valid = false;
        if (started)
          pendingSpace = true;
        continue;
      }
      if (!started && pos == 0 && c == 0xef && at + 1 < source.size() &&
          source.byte(at) == 0xbb && source.byte(at + 1) == 0xbf) {
        at += 2;
        continue;
      }
      if (!started) {
        out.offset = pos;
        started = true;
      }
      if (pendingSpace && n) {
        if (n < 192)
          out.title[n++] = ' ';
        else
          out.valid = false;
        pendingSpace = false;
      }
      if (n < 192)
        out.title[n++] = char(c);
      else
        out.valid = false;
      if (c < 32 || c == 127 || !utf.feed(uint8_t(c)))
        out.valid = false;
    }
    out.title[n] = 0;
    out.valid = out.valid && utf.complete();
    return true;
  }
  static void lower(const char *from, char (&to)[193]) {
    size_t i = 0, j = 0;
    while (from[i] && j < 192) {
      uint8_t a = from[i++];
      if (a < 128)
        to[j++] = a >= 'A' && a <= 'Z' ? a + 32 : a;
      else if ((a == 0xd0 || a == 0xd1) && from[i]) {
        uint8_t b = from[i++];
        unsigned cp = ((a & 31) << 6) | (b & 63);
        if (cp >= 0x410 && cp <= 0x42f)
          cp += 32;
        else if (cp == 0x401)
          cp = 0x451;
        to[j++] = 0xc0 | (cp >> 6);
        to[j++] = 0x80 | (cp & 63);
      } else
        to[j++] = a;
    }
    to[j] = 0;
  }
  static const char *word(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0 &&
                   (!s[n] || s[n] == ' ' || s[n] == ':' || s[n] == '.')
               ? s + n
               : nullptr;
  }
  static bool roman(const char *s, size_t n) {
    if (!n || n > 15)
      return false;
    auto value = [](char c) {
      switch (c) {
      case 'i':
        return 1;
      case 'v':
        return 5;
      case 'x':
        return 10;
      case 'l':
        return 50;
      case 'c':
        return 100;
      case 'd':
        return 500;
      case 'm':
        return 1000;
      default:
        return 0;
      }
    };
    int total = 0;
    for (size_t i = 0; i < n; ++i) {
      int v = value(s[i]);
      if (!v)
        return false;
      total += (i + 1 < n && v < value(s[i + 1])) ? -v : v;
    }
    if (total < 1 || total > 3999)
      return false;
    constexpr int values[] = {1000, 900, 500, 400, 100, 90, 50,
                              40,   10,  9,   5,   4,   1};
    const char *symbols[] = {"m",  "cm", "d",  "cd", "c",  "xc", "l",
                             "xl", "x",  "ix", "v",  "iv", "i"};
    size_t at = 0;
    for (unsigned k = 0; k < 13; ++k)
      while (total >= values[k]) {
        for (const char *p = symbols[k]; *p; ++p)
          if (at == n || s[at++] != *p)
            return false;
        total -= values[k];
      }
    return at == n;
  }
  static bool ordinal(const char *s) {
    while (*s == ' ' || *s == ':' || *s == '.')
      ++s;
    const char *end = s;
    while (*end && *end != ' ' && *end != '.' && *end != ':' && *end != '-')
      ++end;
    size_t n = end - s;
    if (!n)
      return false;
    bool numeric = true;
    for (size_t i = 0; i < n; ++i)
      numeric = numeric && s[i] >= '0' && s[i] <= '9';
    if (numeric || roman(s, n))
      return true;
    for (const char *v :
         {"первая",       "вторая",       "третья",        "четвёртая",
          "четвертая",    "пятая",        "шестая",        "седьмая",
          "восьмая",      "девятая",      "десятая",       "одиннадцатая",
          "двенадцатая",  "тринадцатая",  "четырнадцатая", "пятнадцатая",
          "шестнадцатая", "семнадцатая",  "восемнадцатая", "девятнадцатая",
          "двадцатая",    "двадцать",     "тридцатая",     "сороковая",
          "пятидесятая",  "шестидесятая", "семидесятая",   "восьмидесятая",
          "девяностая",   "сотая"})
      if (strlen(v) == n && !strncmp(s, v, n))
        return true;
    return false;
  }
  static Match match(const char *title) {
    if (!*title)
      return {};
    char s[193];
    lower(title, s);
    for (const char *key :
         {"глава", "часть", "книга", "chapter", "part", "book"}) {
      auto rest = word(s, key);
      if (rest && ordinal(rest))
        return {Strong, strcmp(key, "глава") && strcmp(key, "chapter"), 0};
    }
    for (const char *key : {"интерлюдия", "пролог", "эпилог", "предисловие",
                            "послесловие", "interlude", "prologue", "epilogue"})
      if (word(s, key))
        return {Strong, false, 0};
    // Bare 1.01 / 2.3.4 headings must consist ONLY of numeric components.
    const char *p = s;
    unsigned parts = 0, first = 0;
    while (*p >= '0' && *p <= '9') {
      unsigned digits = 0, value = 0;
      do {
        value = value * 10 + (*p++ - '0');
        ++digits;
      } while (*p >= '0' && *p <= '9' && digits < 5);
      if (digits > 4)
        return {};
      if (!parts)
        first = value;
      ++parts;
      if (*p != '.')
        break;
      ++p;
      if (!*p)
        break;
    }
    if (!*p && parts) {
      if (parts >= 2 && parts <= 4 && first <= 99)
        return {Dotted, false, uint8_t(parts - 1)};
      if (parts == 1 && first >= 1 && first <= 999)
        return {Arabic, false, 0};
    }
    if (roman(s, strlen(s)))
      return {Roman, false, 0};
    return {};
  }

public:
  uint32_t count = 0;
  bool truncated = false;
  template <class Tick> bool scan(TextSource &source, Tick tick) {
    count = 0;
    hasParts = false;
    truncated = false;
    uint32_t seen[5]{};
    uint32_t at = 0;
    while (at < source.size()) {
      Line text;
      if (!line(source, at, text, tick))
        return false;
      if (!text.valid)
        continue;
      auto m = match(text.title);
      if (m.kind == None)
        continue;
      ++seen[m.kind];
      hasParts = hasParts || m.part;
      if (count < starts.size())
        starts[count++] = text.offset;
      else
        truncated = true;
    }
    uint32_t retained = 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t pos = starts[i];
      Line text;
      if (!line(source, pos, text, [] {}))
        return false;
      auto m = match(text.title);
      if (m.kind == Strong || seen[m.kind] >= 2)
        starts[retained++] = starts[i];
    }
    count = retained;
    return true;
  }
  bool scan(TextSource &source) {
    return scan(source, [] {});
  }
  bool chapterAt(TextSource &source, uint32_t index, Chapter &out) const {
    if (index >= count)
      return false;
    uint32_t at = starts[index];
    Line text;
    if (!line(source, at, text, [] {}) || !text.valid)
      return false;
    auto m = match(text.title);
    out.offset = starts[index];
    out.depth =
        m.part
            ? 0
            : std::min<unsigned>(3, (hasParts ? 1 : 0) +
                                        (m.kind == Dotted ? m.levels - 1 : 0));
    memcpy(out.title, text.title, sizeof out.title);
    return true;
  }
};
} // namespace inh
