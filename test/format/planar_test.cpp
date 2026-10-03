#include "host/format/planar.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace hp2::host {
namespace {

TEST(Planar, DeinterleavesLeftmostPixelFromBit7) {
  EXPECT_EQ(DeinterleaveShift(0b1000'0001), (std::array<std::uint8_t, 8>{1, 0, 0, 0, 0, 0, 0, 1}));
  EXPECT_EQ(DeinterleaveShift(0b0110'0000, 3), (std::array<std::uint8_t, 8>{0, 8, 8, 0, 0, 0, 0, 0}));
  EXPECT_EQ(DeinterleaveMask(0b0000'1010, 0x05), (std::array<std::uint8_t, 8>{0, 0, 0, 0, 5, 0, 5, 0}));
}

}  // namespace
}  // namespace hp2::host
