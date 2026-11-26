#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/functional/any_invocable.h>
#include <absl/time/time.h>

namespace fasterswiper {

enum class AnimationTickResult {
  kContinue,
  kFinish,
};

enum class AnimationStopReason {
  kFinished,
  kCancelled,
};

constexpr std::string_view AnimationStopReasonToString(
    AnimationStopReason stop_reason) {
  switch (stop_reason) {
    case AnimationStopReason::kFinished:
      return "kFinished";
    case AnimationStopReason::kCancelled:
      return "kCancelled";
  }
}

struct AnimationTimerParameters {
  // Target display for display-synced timers; std::nullopt falls back to the
  // screen under the mouse or the main screen.
  std::optional<CGDirectDisplayID> display_id;

  // Nominal animation duration (used for watchdog sizing and immediate-tick
  // handling when <= 0).
  absl::Duration duration = absl::ZeroDuration();

  // Fallback tick rate used when a hardware display link is unavailable.
  int64_t fallback_ticks_per_second = 240;

  bool fire_immediately = false;

  absl_nonnull absl::AnyInvocable<AnimationTickResult(
      int64_t /* time_since_timer_start_ns */)>
      tick_callback;
  absl::AnyInvocable<void(AnimationStopReason) &&> stopped_callback;
};

class AnimationTimer {
public:
  AnimationTimer() = default;
  virtual ~AnimationTimer() = default;

  AnimationTimer(const AnimationTimer&) = delete;
  AnimationTimer(AnimationTimer&&) = delete;
  AnimationTimer& operator=(const AnimationTimer&) = delete;
  AnimationTimer& operator=(AnimationTimer&&) = delete;

  // Cancels the timer. Blocks until the timer is guaranteed to no longer be
  // executing any callbacks.
  virtual void Cancel() = 0;
};

using AnimationTimerFactory =
    absl::AnyInvocable<std::unique_ptr<AnimationTimer>(AnimationTimerParameters)
                           const>;

}  // namespace fasterswiper
