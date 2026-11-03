#pragma once
#include <cstdint>
namespace inh {
inline bool needsRefresh(uint32_t requested, uint32_t shown, bool fault) {
  return !fault && requested != shown;
}
class SleepGate {
  uint32_t target_, released_ = 0;
  bool releaseStarted_ = false;

public:
  explicit SleepGate(uint32_t target) : target_(target) {}
  bool ready(uint32_t shown, bool busy, bool down, uint32_t now) {
    if (shown != target_ || busy || down) {
      releaseStarted_ = false;
      return false;
    }
    if (!releaseStarted_) {
      released_ = now;
      releaseStarted_ = true;
      return false;
    }
    return uint32_t(now - released_) >= 80;
  }
};
} // namespace inh
