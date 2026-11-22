#include "src/engine/spring-math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include <absl/log/check.h>

namespace fasterswiper {

namespace {

// Distance threshold below which the tracking safety margin tapers to zero so
// FP16.16 truncation never stalls the final 1-LSB approach to target.
constexpr double kSafetyMarginCutoff = 0.002;

template <std::size_t N>
std::array<PrecomputedStepRow, N> MakeStepTable(double horizon_s) {
  std::array<PrecomputedStepRow, N> table{};
  std::size_t step = 0;
  for (PrecomputedStepRow& row : table) {
    ++step;
    const double tau =
        horizon_s * (static_cast<double>(step) / static_cast<double>(N));
    row = PrecomputedStepRow{
        .tau = tau,
        .a_y = StepSpringExact(SpringState{.y = 1.0, .v = 0.0}, 0.0, tau,
                               kWindowManagerTrackingSpring)
                   .y,
        .a_v = StepSpringExact(SpringState{.y = 0.0, .v = 1.0}, 0.0, tau,
                               kWindowManagerTrackingSpring)
                   .y,
        .y_step = StepSpringExact(SpringState{.y = 0.0, .v = 0.0}, 1.0, tau,
                                  kWindowManagerTrackingSpring)
                      .y,
    };
  }
  return table;
}

}  // namespace

SpringState StepSpringExact(SpringState state, double target, double dt,
                            const SpringParams& params) {
  CHECK_GT(params.response_sec, 0.0);
  CHECK_GT(params.damping_ratio, 0.0);
  CHECK_LT(params.damping_ratio, 1.0);

  if (dt <= 0.0) {
    return state;
  }

  const double gamma = params.gamma();
  const double wd = params.damped_omega();
  const double e0 = state.y - target;
  const double b = gamma * e0 + state.v;
  const double decay = std::exp(-gamma * dt);
  const double s = std::sin(wd * dt);
  const double co = std::cos(wd * dt);
  const double d9 = e0 * co + (b / wd) * s;
  const double d13 = b * co - wd * e0 * s;
  const double d10 = d13 - gamma * d9;

  return SpringState{.y = target + decay * d9, .v = decay * d10};
}

const std::array<PrecomputedStepRow, kLookaheadSamples>&
GetLookaheadStepTable() {
  static const auto kTable =
      MakeStepTable<kLookaheadSamples>(kLookaheadHorizonSec);
  return kTable;
}

const std::array<PrecomputedStepRow, kSafetyTrackSamples>&
GetSafetyStepTable() {
  static const auto kTable =
      MakeStepTable<kSafetyTrackSamples>(kSafetyLookaheadSec);
  return kTable;
}

double ComputeAnalyticalPeakOvershoot(SpringState state, double target,
                                      int direction) {
  auto signed_overshoot = [target, direction](double y) {
    return (direction > 0) ? (y - target) : (target - y);
  };

  double peak = std::max(0.0, signed_overshoot(state.y));

  const double w0 = kWindowManagerTrackingSpring.omega0();
  const double gamma = kWindowManagerTrackingSpring.gamma();
  const double wd = kWindowManagerTrackingSpring.damped_omega();
  const double e0 = state.y - target;
  const double v0 = state.v;
  const double numer = wd * v0;
  const double denom = w0 * w0 * e0 + gamma * v0;

  if (std::abs(numer) > 1e-15 || std::abs(denom) > 1e-15) {
    double theta = std::atan2(numer, denom);
    while (theta <= 0.0) {
      theta += std::numbers::pi;
    }

    // For an underdamped spring, stationary points occur every half-period
    // pi / wd with alternating signs and strictly decaying magnitude, so the
    // global [0, infinity) maximum lies among t = 0, theta / wd, and
    // (theta + pi) / wd.
    const double t0 = theta / wd;
    const double t1 = (theta + std::numbers::pi) / wd;
    peak = std::max(
        peak, signed_overshoot(StepSpringExact(state, target, t0,
                                               kWindowManagerTrackingSpring)
                                   .y));
    peak = std::max(
        peak, signed_overshoot(StepSpringExact(state, target, t1,
                                               kWindowManagerTrackingSpring)
                                   .y));
  }

  return peak;
}

double ComputeTrackingSafetyCap(SpringState state, double target, int direction,
                                double margin, double min_u) {
  const double dist = std::abs(target - state.y);
  const double m_trk =
      (dist > kSafetyMarginCutoff) ? margin * std::min(1.0, dist) : 0.0;
  const double limit = (direction > 0) ? (target - m_trk) : (target + m_trk);
  double u_bound = target;

  for (const PrecomputedStepRow& row : GetSafetyStepTable()) {
    if (row.y_step > 1e-6) {
      const double y_free = row.a_y * state.y + row.a_v * state.v;
      const double candidate = (limit - y_free) / row.y_step;
      u_bound = (direction > 0) ? std::min(u_bound, candidate)
                                : std::max(u_bound, candidate);
    }
  }

  double clamped = (direction > 0)
                       ? std::clamp(u_bound, std::min(min_u, target), target)
                       : std::clamp(u_bound, target, 1.0);
  if (std::abs(target - clamped) < kFixedFP1616Lsb) {
    clamped = target;
  }
  return clamped;
}

}  // namespace fasterswiper
