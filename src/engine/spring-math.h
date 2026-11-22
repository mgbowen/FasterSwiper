#pragma once

#include "src/fp1616.h"

#include <array>
#include <cmath>
#include <numbers>

namespace fasterswiper {

// Underdamped second-order unit-mass spring parameters (0 < damping_ratio < 1,
// response_sec > 0).
struct SpringParams {
  double damping_ratio = 0.86;
  double response_sec = 0.15;

  [[nodiscard]] constexpr double omega0() const {
    return (2.0 * std::numbers::pi) / response_sec;
  }

  [[nodiscard]] constexpr double gamma() const {
    return damping_ratio * omega0();
  }

  [[nodiscard]] double damped_omega() const {
    return omega0() * std::sqrt(1.0 - damping_ratio * damping_ratio);
  }

  [[nodiscard]] constexpr double lag_time() const {
    return (2.0 * damping_ratio) / omega0();
  }
};

// Identified macOS 27 WindowManager.app .tracking spring:
// Spring(dampingRatio: 0.86, response: 0.15), tau_lag = 41.0620 ms.
inline constexpr SpringParams kWindowManagerTrackingSpring{
    .damping_ratio = 0.86,
    .response_sec = 0.15,
};

struct SpringState {
  double y = 0.0;
  double v = 0.0;
};

// Precomputed linear superposition coefficients for
// kWindowManagerTrackingSpring over offset `tau`:
//   y(tau; y0, v0, u) = a_y * y0 + a_v * v0 + y_step * u.
struct PrecomputedStepRow {
  double tau = 0.0;
  double a_y = 0.0;
  double a_v = 0.0;
  double y_step = 0.0;
};

inline constexpr int kLookaheadSamples = 8;
inline constexpr double kLookaheadHorizonSec = 0.045;
inline constexpr int kSafetyTrackSamples = 16;
inline constexpr double kSafetyLookaheadSec = 0.145;

// Exact closed-form ODE step of a unit-mass underdamped spring (0 < zeta < 1).
[[nodiscard]] SpringState StepSpringExact(
    SpringState state, double target, double dt,
    const SpringParams& params = kWindowManagerTrackingSpring);

// Precomputed step tables for kWindowManagerTrackingSpring.
[[nodiscard]] const std::array<PrecomputedStepRow, kLookaheadSamples>&
GetLookaheadStepTable();
[[nodiscard]] const std::array<PrecomputedStepRow, kSafetyTrackSamples>&
GetSafetyStepTable();

// Exact O(1) global [0, infinity) peak overshoot of holding `u = target` from
// `state` in `direction` (+1 or -1) for kWindowManagerTrackingSpring.
[[nodiscard]] double ComputeAnalyticalPeakOvershoot(SpringState state,
                                                    double target,
                                                    int direction);

// Tightest constant command in [min_u, target] (direction > 0) or
// [target, 1.0] (direction < 0) keeping the 16-sample 0.145 s tracking
// response of kWindowManagerTrackingSpring within distance-tapered `margin`.
[[nodiscard]] double ComputeTrackingSafetyCap(SpringState state, double target,
                                              int direction, double margin,
                                              double min_u = kFixedFP1616Lsb);

}  // namespace fasterswiper
