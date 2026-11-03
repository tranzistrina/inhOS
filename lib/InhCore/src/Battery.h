#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace inh {
// Approximate rested single-cell LiPo SOC. This is not a fuel gauge.
inline int batteryPercent(unsigned mv) {
  if (mv < 2500 || mv > 4400)
    return -1;
  constexpr unsigned volts[] = {3300, 3500, 3600, 3700, 3800,
                                3900, 4000, 4100, 4200};
  constexpr int levels[] = {0, 5, 10, 25, 45, 65, 80, 90, 100};
  if (mv <= volts[0])
    return 0;
  for (unsigned i = 1; i < 9; ++i)
    if (mv <= volts[i])
      return levels[i - 1] + (mv - volts[i - 1]) * (levels[i] - levels[i - 1]) /
                                 (volts[i] - volts[i - 1]);
  return 100;
}
struct Battery {
  static constexpr uint32_t Interval = 60000;
  uint32_t attemptedAt = 0;
  unsigned millivolts = 0;
  int percent = -1;
  bool attempted = false;
  bool due(uint32_t now, bool wifi) const {
    return !wifi && (!attempted || uint32_t(now - attemptedAt) >= Interval);
  }
  // Hardware adapter returns nine calibrated battery-voltage samples.
  // Failed/invalid samples invalidate the reading, never masquerade as 0%.
  void update(uint32_t now, std::array<unsigned, 9> samples, bool ok) {
    attempted = true;
    attemptedAt = now;
    for (auto mv : samples)
      ok = ok && batteryPercent(mv) >= 0;
    std::sort(samples.begin(), samples.end());
    millivolts = ok ? samples[4] : 0;
    percent = ok ? batteryPercent(millivolts) : -1;
  }
  void label(char (&out)[8], bool wifi) const {
    if (percent < 0)
      std::snprintf(out, sizeof out, "--%%");
    else
      std::snprintf(out, sizeof out, "%s%d%%", wifi ? "~" : "", percent);
  }
};
} // namespace inh
