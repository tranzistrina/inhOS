#include "Book.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
struct Source : inh::TextSource {
  std::string bytes;
  uint32_t limit = UINT32_MAX;
  uint32_t size() const override {
    return std::min<uint32_t>(bytes.size(), limit);
  }
  int byte(uint32_t p) override { return p < size() ? uint8_t(bytes[p]) : -1; }
};
void buttons() {
  using G = inh::Gesture;
  for (unsigned count = 1; count <= 4; ++count) {
    inh::Button b;
    b.reset(0, false);
    for (unsigned i = 0; i < count; ++i) {
      uint32_t t = i * 160;
      assert(b.tick(t + 1, true) == G::None);
      assert(b.tick(t + 35, true) == G::None);
      assert(b.tick(t + 80, false) == G::None);
      auto ev = b.tick(t + 115, false);
      assert(ev == (i == 3 ? G::Quadruple : G::None));
    }
    auto ev = b.tick(count * 160 + 360, false);
    assert(ev == (count == 1   ? G::Single
                  : count == 2 ? G::Double
                  : count == 3 ? G::Triple
                               : G::None));
  }
  inh::ContentsState state;
  state.enter(157, 89, 35);
  state.move();
  state.move(true);
  assert(state.selection == 35 && state.cancel() == 157);
  state.enter(91, 0);
  state.move();
  state.move(true);
  assert(state.selection == 0 && state.cancel() == 91);
  state.enter(0, 89);
  state.move(true);
  assert(state.selection == 88);
  state.move();
  assert(state.selection == 0);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  buttons();
  std::ifstream f(argv[1], std::ios::binary);
  assert(f);
  Source raw;
  raw.bytes.assign(std::istreambuf_iterator<char>(f), {});
  inh::BookLayout book;
  if (!inh::parseBook(raw, book)) {
    std::cerr << "invalid book\n";
    return 2;
  }
  inh::ChapterCursor cursor;
  Source limited;
  limited.bytes = raw.bytes;
  std::vector<uint32_t> pages;
  uint32_t offset = book.begin;
  while (offset < book.end) {
    assert(cursor.advance(raw, book, offset));
    limited.limit = std::min(cursor.next, book.end);
    pages.push_back(offset);
    auto p = inh::paginate(limited, offset, 40, 8);
    assert(p.next > offset && p.next <= book.end);
    offset = p.next;
  }
  for (uint32_t i = 0; i < book.chapters; ++i) {
    inh::Chapter chapter;
    assert(inh::chapterAt(raw, book, i, chapter));
    uint32_t page;
    assert(inh::pageForOffset(
        pages.size(), chapter.offset,
        [&](uint32_t n, uint32_t &at) {
          at = pages.at(n);
          return true;
        },
        page));
    assert(pages[page] == chapter.offset); // Every TOC target starts its page.
    limited.limit = page + 1 < pages.size() ? pages[page + 1] : book.end;
    auto p = inh::paginate(limited, pages[page], 40, 8);
    assert(!p.lines.empty());
  }
  std::cout << "{\"chapters\":" << book.chapters
            << ",\"pages\":" << pages.size() << ",\"begin\":" << book.begin
            << ",\"end\":" << book.end << "}\n";
}
