#include "Battery.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

int main() {
  assert(inh::batteryPercent(2499) == -1);
  assert(inh::batteryPercent(4401) == -1);
  assert(inh::batteryPercent(3300) == 0);
  assert(inh::batteryPercent(4200) == 100);
  assert(inh::batteryPercent(3800) == 45);
  assert(inh::batteryPercent(3850) == 55);
  for (unsigned mv = 2501; mv <= 4400; ++mv)
    assert(inh::batteryPercent(mv) >= inh::batteryPercent(mv - 1));
  inh::Battery b;
  char label[8];
  b.label(label, false);
  assert(!strcmp(label, "--%"));
  assert(b.due(0, false) && !b.due(0, true));
  b.update(50, {3800, 3800, 3800, 3800, 3800, 4200, 3300, 3790, 3810}, true);
  assert(b.percent == 45 && b.millivolts == 3800); // rejects outlier influence
  assert(!b.due(60049, false) && b.due(60050, false));
  assert(!b.due(60050, true)); // Wi-Fi never permits ADC2 sampling
  b.label(label, true);
  assert(!strcmp(label, "~45%"));
  b.update(0, {4200, 4200, 4200, 4200, 4200, 4200, 4200, 4200, 4200}, true);
  b.label(label, true);
  assert(!strcmp(label, "~100%") && 214 + strlen(label) * 6 <= 250);
  b.update(1, {3800, 3800, 3800, 3800, 3800, 3800, 3800, 3800, 0}, true);
  assert(b.percent == -1 && b.millivolts == 0);
  b.update(2, {3800, 3800, 3800, 3800, 3800, 3800, 3800, 3800, 3800}, false);
  b.label(label, true);
  assert(!strcmp(label, "--%"));
  b.attemptedAt = std::numeric_limits<uint32_t>::max() - 30000;
  assert(!b.due(10000, false) && b.due(40000, false));
  std::cout << "PASS: LiPo curve, invalid ADC failure, median, fixed badge "
               "bounds, Wi-Fi cache, millis wrap\n";
}
