#pragma once
#include "Reader.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace inh {
constexpr size_t ImageBytes = 3813, GinhHeaderBytes = 20, GinhBytes = 3833;
using Image = std::array<uint8_t, ImageBytes>;
inline uint16_t le16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t le32(const uint8_t *p) {
  return le16(p) | uint32_t(le16(p + 2)) << 16;
}
inline bool decodeGinh(const uint8_t *data, size_t size, Image &out) {
  if (size != GinhBytes || data[0] != 'G' || data[1] != 'I' || data[2] != 'N' ||
      data[3] != 'H' || data[4] != 1 || data[5] != 20 ||
      le16(data + 6) != 250 || le16(data + 8) != 122 || le16(data + 10) != 0 ||
      le32(data + 12) != ImageBytes)
    return false;
  const auto *pixels = data + 20;
  if ((pixels[3812] & 15) != 0 ||
      (crc32(pixels, ImageBytes) ^ 0xffffffffu) != le32(data + 16))
    return false;
  std::copy(pixels, pixels + ImageBytes, out.begin());
  return true;
}
inline bool imageBlack(const Image &image, unsigned x, unsigned y) {
  const auto bit = y * 250 + x;
  return image[bit / 8] & (0x80 >> (bit % 8));
}
} // namespace inh
