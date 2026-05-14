#pragma once

#include <cfloat>
#include <cstdint>

namespace fasterswiper {

constexpr double kEpsilon = FLT_TRUE_MIN;
constexpr double kInstantSwitchVelocity = 50;
constexpr int64_t kOneSwipeInNanoswipes = 1'000'000;

} // namespace fasterswiper
