#pragma once
namespace inh {
struct Point {
  int x, y;
};
// Logical 250x122 canvas -> panel pixel; independent of panel driver revision.
constexpr Point orientPixel(int x, int y, bool flipped) {
  return flipped ? Point{249 - x, 121 - y} : Point{x, y};
}
} // namespace inh
