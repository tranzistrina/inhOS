#include "Files.h"
#include "Ginh.h"
#include "SleepGate.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  inh::Image p{};
  assert(inh::decodeGinh(b.data(), b.size(), p));
  for (size_t i = 0; i < b.size(); i += 31) {
    b[i] ^= 1;
    assert(!inh::decodeGinh(b.data(), b.size(), p));
    b[i] ^= 1;
  }
  for (size_t n = 0; n < b.size(); n++)
    assert(!inh::decodeGinh(b.data(), n, p));
  b.push_back(0);
  assert(!inh::decodeGinh(b.data(), b.size(), p));
  assert(inh::userPath("/a.inh") && inh::userPath("/a.ginh") &&
         inh::userPath("/book.txt"));
  for (auto s : {"/../a.inh", "/a/b.inh", "/book.idx", "/upload.tmp",
                 "/.ginh/../", "/nvs", "//a.inh", "/a.inh?delete"})
    assert(!inh::userPath(s));
  inh::SourceValidator utf8;
  for (unsigned char c : std::string("аще истина:\n"))
    assert(utf8.feed(c));
  assert(utf8.complete());
  for (auto bad : {std::string("\0", 1), std::string("\x80"),
                   std::string("\xc0\xaf"), std::string("\xed\xa0\x80")}) {
    inh::SourceValidator v;
    bool valid = true;
    for (unsigned char c : bad)
      if (!v.feed(c))
        valid = false;
    assert(!valid || !v.complete());
  }
  inh::SourceValidator truncated;
  assert(truncated.feed(0xe2));
  assert(!truncated.complete());
  // Actual queue dedup predicate and sleep gate: one final refresh, no sleep
  // while held or busy.
  uint32_t shown = 9, target = 10;
  int updates = 0;
  for (int i = 0; i < 4; i++)
    if (inh::needsRefresh(target, shown, false)) {
      updates++;
      shown = target;
    }
  assert(updates == 1);
  inh::SleepGate gate(target);
  assert(!gate.ready(9, false, false, 0));
  assert(!gate.ready(10, true, false, 10));
  assert(!gate.ready(10, false, true, 10000));
  assert(!gate.ready(10, false, false, 10010));
  assert(!gate.ready(10, false, false, 10089));
  assert(gate.ready(10, false, false, 10090));
  assert(!gate.ready(10, false, true, 10091));
  std::cout << "PASS: real GINH decoder CRC/size/header/corruption; path "
               "policy; dedup final update + release-to-arm sleep coordinator "
               "(hardware mocked)\n";
}
