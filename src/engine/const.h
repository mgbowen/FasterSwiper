#pragma once

#include <cstdint>

namespace fasterswiper {

// Smallest representable non-zero value in a fixed-point 16.16 32-bit signed
// integer.
constexpr double kEpsilon = 0.000016;

constexpr double kInstantSwitchVelocity = 500;

constexpr int64_t kOneSwipeInNanoswipes = 1'000'000;

} // namespace fasterswiper
