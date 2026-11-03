#pragma once
#include <cstdint>
#include <string>
namespace inh {
class SourceValidator {
  unsigned need_ = 0;
  uint32_t cp_ = 0, min_ = 0;

public:
  bool feed(uint8_t b) {
    if (!need_) {
      if (b < 128)
        return b != 0;
      if (b >= 0xc2 && b <= 0xdf) {
        need_ = 1;
        cp_ = b & 31;
        min_ = 0x80;
        return true;
      }
      if (b >= 0xe0 && b <= 0xef) {
        need_ = 2;
        cp_ = b & 15;
        min_ = 0x800;
        return true;
      }
      if (b >= 0xf0 && b <= 0xf4) {
        need_ = 3;
        cp_ = b & 7;
        min_ = 0x10000;
        return true;
      }
      return false;
    }
    if ((b & 0xc0) != 0x80)
      return false;
    cp_ = (cp_ << 6) | (b & 63);
    if (--need_ == 0)
      return cp_ >= min_ && cp_ <= 0x10ffff &&
             !(cp_ >= 0xd800 && cp_ <= 0xdfff);
    return true;
  }
  bool complete() const { return need_ == 0; }
};

inline bool suffix(const std::string &s, const char *ext) {
  const std::string e = ext;
  return s.size() >= e.size() &&
         s.compare(s.size() - e.size(), e.size(), e) == 0;
}
inline bool userPath(const std::string &p) {
  if (p.empty() || p.size() > 37 || p[0] != '/' ||
      p.find("..") != std::string::npos || p.find('/', 1) != std::string::npos)
    return false;
  if (p == "/book.txt")
    return true;
  if (!suffix(p, ".inh") && !suffix(p, ".ginh"))
    return false;
  for (size_t i = 1; i < p.size(); i++)
    if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
          (p[i] >= '0' && p[i] <= '9') || p[i] == '_' || p[i] == '-' ||
          p[i] == '.'))
      return false;
  return true;
}
} // namespace inh
