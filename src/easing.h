#pragma once

#include <functional>

#include "third_party/chromium/cubic-bezier.h"

namespace fasterswiper {

using EasingFunction = std::function<double(double)>;

EasingFunction MakeEasingFunctionLinear();
EasingFunction MakeEasingFunctionEaseOutQuadratic();
EasingFunction MakeEasingFunctionEaseOutQuintic();
EasingFunction
MakeEasingFunctionBezier(third_party::chromium::gfx::CubicBezier bezier);

} // namespace fasterswiper
