#include "src/easing.h"

#include <absl/status/status.h>

namespace fasterswiper {

namespace {

constexpr double EasingFunctionLinear(double t) { return t; }

constexpr double EasingFunctionEaseOutQuadratic(double t) {
  double inv = 1.0 - t;
  return 1.0 - inv * inv;
}

constexpr double EasingFunctionEaseOutQuintic(double t) {
  double inv = 1.0 - t;
  return 1.0 - inv * inv * inv * inv * inv;
}

} // namespace

EasingFunction MakeEasingFunctionLinear() { return EasingFunctionLinear; }

EasingFunction MakeEasingFunctionEaseOutQuadratic() {
  return EasingFunctionEaseOutQuadratic;
}

EasingFunction MakeEasingFunctionEaseOutQuintic() {
  return EasingFunctionEaseOutQuintic;
}

EasingFunction
MakeEasingFunctionBezier(third_party::chromium::gfx::CubicBezier bezier) {
  return [bezier = std::move(bezier)](double t) -> double {
    return bezier.Solve(t);
  };
}

absl::StatusOr<EasingFunction>
FromDaemonOptions(const proto::DaemonOptions &options) {
  switch (options.easing_function()) {
  case proto::EASING_FUNCTION_LINEAR:
    return MakeEasingFunctionLinear();
  case proto::EASING_FUNCTION_QUADRATIC_EASE_OUT:
    return MakeEasingFunctionEaseOutQuadratic();
  case proto::EASING_FUNCTION_QUINTIC_EASE_OUT:
    return MakeEasingFunctionEaseOutQuintic();
  case proto::EASING_FUNCTION_CUBIC_BEZIER_CURVE:
    return MakeEasingFunctionBezier(third_party::chromium::gfx::CubicBezier(
        options.cubic_bezier_curve().p1x(), options.cubic_bezier_curve().p1y(),
        options.cubic_bezier_curve().p2x(),
        options.cubic_bezier_curve().p2y()));
  }

  return absl::InvalidArgumentError("Invalid easing_function in DaemonOptions");
}

} // namespace fasterswiper
