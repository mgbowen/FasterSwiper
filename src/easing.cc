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

}  // namespace

EasingFunction MakeEasingFunctionLinear() { return EasingFunctionLinear; }

EasingFunction MakeEasingFunctionEaseOutQuadratic() {
  return EasingFunctionEaseOutQuadratic;
}

EasingFunction MakeEasingFunctionEaseOutQuintic() {
  return EasingFunctionEaseOutQuintic;
}

EasingFunction MakeEasingFunctionBezier(
    const third_party::chromium::gfx::CubicBezier& bezier) {
  return [bezier](double t) -> double { return bezier.Solve(t); };
}

absl::StatusOr<EasingFunction> FromGestureSettings(
    proto::EasingFunction easing_function,
    const proto::CubicBezierCurve& cubic_bezier_curve) {
  switch (easing_function) {
    case proto::EASING_FUNCTION_LINEAR:
      return MakeEasingFunctionLinear();
    case proto::EASING_FUNCTION_QUADRATIC_EASE_OUT:
      return MakeEasingFunctionEaseOutQuadratic();
    case proto::EASING_FUNCTION_QUINTIC_EASE_OUT:
      return MakeEasingFunctionEaseOutQuintic();
    case proto::EASING_FUNCTION_CUBIC_BEZIER_CURVE:
      return MakeEasingFunctionBezier(third_party::chromium::gfx::CubicBezier(
          cubic_bezier_curve.p1x(), cubic_bezier_curve.p1y(),
          cubic_bezier_curve.p2x(), cubic_bezier_curve.p2y()));
  }

  return absl::InvalidArgumentError(
      "Invalid easing_function in GestureSettings");
}

absl::StatusOr<EasingFunction> FromGestureSettings(
    const proto::GestureSettings& settings) {
  return FromGestureSettings(settings.easing_function(),
                             settings.cubic_bezier_curve());
}

absl::StatusOr<EasingFunction> FromDaemonOptions(
    const proto::DaemonOptions& options) {
  if (options.has_horizontal_settings()) {
    return FromGestureSettings(options.horizontal_settings());
  }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return FromGestureSettings(options.easing_function(),
                             options.cubic_bezier_curve());
#pragma clang diagnostic pop
}

}  // namespace fasterswiper
