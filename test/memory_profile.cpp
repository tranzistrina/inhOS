#include "Glagol.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>
static size_t live = 0, peak = 0, calls = 0;
static long failAt = -1;
struct alignas(std::max_align_t) Header {
  size_t size;
};
void *operator new(size_t n) {
  if (failAt == long(calls++)) {
    failAt = -1;
    throw std::bad_alloc();
  }
  auto *p = static_cast<Header *>(std::malloc(sizeof(Header) + n));
  if (!p)
    throw std::bad_alloc();
  p->size = n;
  live += n;
  if (live > peak)
    peak = live;
  return p + 1;
}
void operator delete(void *p) noexcept {
  if (p) {
    auto *h = static_cast<Header *>(p) - 1;
    live -= h->size;
    std::free(h);
  }
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, size_t) noexcept { ::operator delete(p); }
int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  std::ifstream f(argv[1]);
  std::stringstream ss;
  ss << f.rdbuf();
  std::string source = ss.str();
  inh::VM vm([](const std::string &, const std::vector<inh::Value> &) {
    return inh::Value(0);
  });
  size_t base = live;
  peak = live;
  for (int i = 0; i < 8; i++) {
    vm.stop();
    vm.start(inh::compile(source));
    for (int n = 0; n < 10000 && vm.running(); n++)
      vm.tick(1);
    assert(vm.error().empty());
    vm.stop();
#ifndef BASELINE
    assert(live == base);
#endif
  }
  std::printf("sizeof(Op)=%zu; 8 compile/run/stop peak requested heap above "
              "baseline=%zu bytes; retained=%zu bytes\n",
              sizeof(inh::Op), peak - base, live - base);
#ifndef BASELINE
  for (int k = 0; k < 180; k++) {
    failAt = long(calls) + k;
    try {
      auto program = inh::compile(source);
      vm.start(std::move(program));
      for (int n = 0; n < 500 && vm.running(); n++)
        vm.tick(1);
    } catch (const std::exception &) {
    }
    failAt = -1;
    vm.stop();
#ifndef BASELINE
    assert(live == base);
#endif
  }
  std::puts("PASS: 180 one-shot allocation failures recovered, no retained app "
            "allocations");
#endif
}
