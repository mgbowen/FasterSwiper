#pragma once

#include "src/animation-timer.h"

#include <atomic>
#include <cstdint>
#include <optional>

#include <absl/functional/any_invocable.h>
#include <dispatch/dispatch.h>

namespace fasterswiper {

int64_t UptimeInNanoseconds();

class PeriodicTimer : public AnimationTimer {
public:
  struct Parameters {
    int64_t period_ns = 0;
    bool fire_immediately = false;
    absl::AnyInvocable<AnimationTickResult(
        int64_t /* time_since_timer_start_ns */)>
        tick_callback;
    absl::AnyInvocable<void(AnimationStopReason) &&> stopped_callback;
  };

  explicit PeriodicTimer(Parameters parameters);

  PeriodicTimer(const PeriodicTimer&) = delete;
  PeriodicTimer(PeriodicTimer&&) = delete;
  PeriodicTimer& operator=(const PeriodicTimer&) = delete;
  PeriodicTimer& operator=(PeriodicTimer&&) = delete;

  ~PeriodicTimer() override;

  // Cancel the timer. This method blocks until the timer is guaranteed to no
  // longer be executing.
  void Cancel() override;

private:
  Parameters params_;

  dispatch_queue_t queue_;
  dispatch_source_t timer_;
  int64_t start_time_ns_ = 0;

  std::atomic_flag stop_requested_ = ATOMIC_FLAG_INIT;
  std::atomic<AnimationStopReason> stop_reason_ =
      AnimationStopReason::kCancelled;
  std::atomic<bool> stopped_ = false;

  void CancelImpl();
  void HandleTick();
};

AnimationTimerFactory MakePeriodicTimerFactory(
    std::optional<int64_t> ticks_per_second_override = std::nullopt);

}  // namespace fasterswiper
