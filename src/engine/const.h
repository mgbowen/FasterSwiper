#pragma once

#include <cstdint>

namespace fasterswiper {

// This must be a small value, but not the smallest possible positive value,
// e.g. `std::numeric_limits<float>::denorm_min()`; everything works except
// rubberbanding downwards in App Expose, which will cause it to close App
// Expose and go back to the Desktop. A larger, but still small value, fixes it.
constexpr double kEpsilon = 1e-15;
constexpr double kFixed1616Epsilon = 0.000016;

constexpr double kInstantSwitchVelocity = 100;

constexpr int64_t kOneSwipeInNanoswipes = 1'000'000;

} // namespace fasterswiper
