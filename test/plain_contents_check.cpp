#include "PlainContents.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
struct Source : inh::TextSource {
  std::string data;
  uint32_t size() const override { return data.size(); }
  int byte(uint32_t at) override {
    return at < data.size() ? uint8_t(data[at]) : -1;
  }
};
void quote(const char *s) {
  std::cout << '"';
  for (const char *p = s; *p; ++p) {
    if (*p == '"' || *p == '\\')
      std::cout << '\\';
    std::cout << *p;
  }
  std::cout << '"';
}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream f(argv[1], std::ios::binary);
  assert(f);
  Source source;
  source.data.assign(std::istreambuf_iterator<char>(f), {});
  auto unchanged = source.data;
  inh::PlainContents toc;
  unsigned yields = 0;
  assert(toc.scan(source, [&] { ++yields; }));
  assert(source.data ==
         unchanged); // no conversion/rewrite or metadata required
  std::vector<uint32_t> pages;
  uint32_t at = 0;
  while (at < source.size()) {
    pages.push_back(at);
    at = inh::paginate(source, at, 40, 8).next;
  }
  std::cout << "{\"count\":" << toc.count
            << ",\"truncated\":" << (toc.truncated ? "true" : "false")
            << ",\"pages\":" << pages.size() << ",\"entries\":[";
  for (uint32_t i = 0; i < toc.count; ++i) {
    inh::Chapter entry;
    assert(toc.chapterAt(source, i, entry));
    uint32_t page = 0;
    assert(inh::pageForOffset(
        pages.size(), entry.offset,
        [&](uint32_t n, uint32_t &v) {
          v = pages.at(n);
          return true;
        },
        page));
    assert(pages[page] <= entry.offset);
    assert(page + 1 == pages.size() || pages[page + 1] > entry.offset);
    inh::ContentsState state;
    state.enter(page, toc.count, i);
    state.move();
    assert(state.cancel() == page);
    if (i)
      std::cout << ',';
    std::cout << "{\"offset\":" << entry.offset
              << ",\"depth\":" << unsigned(entry.depth) << ",\"page\":" << page
              << ",\"title\":";
    quote(entry.title);
    std::cout << '}';
  }
  std::cout << "]}\n";
}
