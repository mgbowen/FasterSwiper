#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fasterswiper {

using FixedFP1616 = int32_t;

inline constexpr double kFixedFP1616Scale = 65536.0;
inline constexpr double kFixedFP1616Lsb = 1.0 / kFixedFP1616Scale;
inline constexpr double kMaxFixedFP1616 = 32767.0;

[[nodiscard]] inline FixedFP1616 DoubleToFixedFP1616(double val) {
  if (std::isnan(val)) {
    return 0;
  }

  const double clamped = std::clamp(val, -kMaxFixedFP1616, kMaxFixedFP1616);
  const auto fixed_val = static_cast<FixedFP1616>(clamped * kFixedFP1616Scale);
  if (fixed_val == 0 && std::abs(clamped) > 0) {
    const FixedFP1616 sign = clamped > 0 ? 1 : -1;
    return sign;
  }

  return fixed_val;
}

[[nodiscard]] inline double QuantizeFP1616(double val) {
  return static_cast<double>(DoubleToFixedFP1616(val)) / kFixedFP1616Scale;
}

}  // namespace fasterswiper
