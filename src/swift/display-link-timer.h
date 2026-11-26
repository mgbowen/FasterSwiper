#pragma once

#include "src/animation-timer.h"

#include <memory>

#include <absl/base/nullability.h>
#include <absl/status/statusor.h>

namespace fasterswiper {

// Creates a CADisplayLink-backed AnimationTimer synced to
// `parameters.display_id` (or the screen under the mouse / main screen).
// Only consumes `parameters` when returning OK; returns a descriptive error
// status without consuming `parameters` otherwise (e.g., kInvalidArgument for
// missing callbacks, kFailedPrecondition if called off the main thread,
// kUnimplemented on unsupported macOS versions, or kUnavailable when no active
// awake NSScreen is available).
absl::StatusOr<absl_nonnull std::unique_ptr<AnimationTimer>>
CreateDisplayLinkTimer(AnimationTimerParameters& parameters);

// Returns an AnimationTimerFactory that drives animations with CADisplayLink
// when available and falls back to PeriodicTimer otherwise (e.g., headless or
// non-main-thread environments).
AnimationTimerFactory MakeDisplayLinkTimerFactory();

}  // namespace fasterswiper
