#include "Glagol.h"
#include "Reader.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
using namespace inh;
struct Source : TextSource {
  std::string s;
  uint32_t size() const override { return s.size(); }
  int byte(uint32_t p) override { return p < s.size() ? (uint8_t)s[p] : -1; }
};
static VM run(const std::string &s) {
  VM v;
  v.start(compile(s));
  unsigned ticks = 0;
  while (v.tick(17)) {
    assert(++ticks < 10000);
  }
  if (!v.error().empty())
    std::cerr << v.error() << '\n';
  assert(v.error().empty());
  return v;
}
int main() {
  Source s;
  for (int i = 0; i < 9000; i++)
    s.s += "Страница " + std::to_string(i) + ": ёжик и яблоко.\n";
  PageIndex idx;
  idx.begin();
  while (idx.extend(s, 40, 8)) {
  }
  assert(idx.starts.size() > 1000);
  assert(idx.validate(s.size()));
  auto original = idx.starts;
  for (size_t page = 1; page < idx.starts.size(); page += 19) {
    PageIndex restored;
    restored.starts = original;
    size_t current = restored.pageAt(original[page]);
    assert(current == page);
    auto prev = paginate(s, restored.starts[current - 1], 40, 8);
    assert(prev.next == original[page]);
  }
  Source longword;
  longword.s = std::string(300, 'a');
  assert(paginate(longword, 0, 40, 2).next == 80);
  Source bad;
  bad.s = "a\xff\xc0\x80";
  auto p = paginate(bad, 0, 40, 8);
  assert(p.next == bad.size());
  Source empty;
  assert(paginate(empty, 0, 40, 8).next == 0);
  std::mt19937 rng(1);
  for (int z = 0; z < 1000; z++) {
    Source random;
    for (int j = 0; j < 200; j++)
      random.s += char(rng() % 256);
    uint32_t at = 0;
    while (at < random.size()) {
      auto pg = paginate(random, at, 1 + rng() % 40, 1 + rng() % 8);
      assert(pg.next > at && pg.next <= random.size());
      at = pg.next;
    }
  }
  auto v = run(
      "а = 2 + 3 * 4\nб = -2 ** 2\nв = 2 ** 3 ** 2\nг = -7 // 3\nд = -7 % 3\n");
  assert(v.variables().at("а").num() == 14);
  assert(v.variables().at("б").num() == -4);
  assert(v.variables().at("в").num() == 512);
  assert(v.variables().at("г").num() == -3);
  assert(v.variables().at("д").num() == 2);
  v = run("сумма = 0\nдля иже въ ряд(100):\n    аще иже == 8:\n        "
          "прерви\n    аще иже == 2:\n        продолжи\n    сумма += иже\n");
  assert(v.variables().at("сумма").num() == 26);
  v = run("а = массив(3)\nа[1] = 9\nб = а\nб[1] = 7\nв = а[1]\nг = ложь и "
          "нетпеременной\nд = истина или нетпеременной\nаще в == 7:\n    е = "
          "1\nинако:\n    е = 2\n");
  assert(v.variables().at("в").num() == 7 && v.variables().at("е").num() == 1);
  v = run("а = 0\nдондеже а < 100:\n    а += 1\n    продолжи\n    а = 999\nб = "
          "синус(пи() / 2)\n");
  assert(v.variables().at("а").num() == 100);
  assert(v.variables().at("б").num() > 0.99999);
  VM infinite;
  infinite.start(compile("дондеже истина:\n    а = 1\n"));
  assert(infinite.tick(100));
  assert(infinite.instructions() == 100);
  infinite.stop();
  assert(!infinite.tick());
  for (auto code : {"а = 1 / 0\n", "а = массив(2048)\n", "а = [1]\nа[1] = 4\n",
                    "а = корень(-1)\n", "а = ряд(1, 5, 0)\n"}) {
    VM e;
    e.start(compile(code));
    while (e.tick()) {
    }
    assert(!e.error().empty());
  }
  for (auto code : {"аще истина:\nа = 1\n", "прерви\n", "а = (1\n",
                    "инако:\n    а = 1\n", " а = 1\n"}) {
    bool threw = false;
    try {
      compile(code);
    } catch (...) {
      threw = true;
    }
    assert(threw);
  }
  Button b;
  b.reset(0, false);
  assert(b.tick(1, true) == Gesture::None);
  b.tick(35, true);
  b.tick(100, false);
  b.tick(135, false);
  assert(b.tick(500, false) == Gesture::Single);
  assert(b.tick(1000, false) == Gesture::None);
  b.reset(0, true);
  b.tick(2000, true);
  b.tick(2010, false);
  assert(b.tick(2050, false) == Gesture::None);
  assert(b.tick(2500, false) == Gesture::None);
  b.reset(0, false);
  b.tick(1, true);
  b.tick(35, true);
  assert(b.tick(1340, true) == Gesture::Hold);
  assert(b.tick(3000, true) == Gesture::None);
  b.tick(3001, false);
  b.tick(3040, false);
  assert(b.tick(3500, false) == Gesture::None);
  b.reset(0, false);
  for (int i = 0; i < 3; i++) {
    int t = i * 160;
    b.tick(t + 1, true);
    b.tick(t + 35, true);
    b.tick(t + 80, false);
    auto ev = b.tick(t + 115, false);
    assert(ev == (i == 2 ? Gesture::Triple : Gesture::None));
  }
  std::cout << "PASS: pagination/restart/1000 random UTF-8 inputs, VM "
               "execution/errors/stop, button wake/hold/triple\n";
}
