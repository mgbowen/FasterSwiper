#include "src/easing.h"

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

} // namespace fasterswiper
