#pragma once
#include <string>
namespace inh {
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
    if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= '0' && p[i] <= '9') ||
          p[i] == '_' || p[i] == '-' || p[i] == '.'))
      return false;
  return true;
}
} // namespace inh
