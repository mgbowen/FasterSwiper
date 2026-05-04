#pragma once

#include "src/public/fasterswiper.pb.h"
#include "third_party/chromium/cubic-bezier.h"

#include <functional>

#include "absl/status/statusor.h"

namespace fasterswiper {

using EasingFunction = std::function<double(double)>;

EasingFunction MakeEasingFunctionLinear();
EasingFunction MakeEasingFunctionEaseOutQuadratic();
EasingFunction MakeEasingFunctionEaseOutQuintic();
EasingFunction
MakeEasingFunctionBezier(third_party::chromium::gfx::CubicBezier bezier);

absl::StatusOr<EasingFunction>
FromDaemonOptions(const proto::DaemonOptions &options);

} // namespace fasterswiper
