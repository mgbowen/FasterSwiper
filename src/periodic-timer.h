#pragma once

#include <atomic>
#include <dispatch/dispatch.h>

#include "absl/functional/any_invocable.h"

namespace fasterswiper {

enum class PeriodicTimerTickResult {
  kContinueTimer,
  kFinishTimer,
};

enum class PeriodicTimerStopReason {
  kUnknown,
  kFinished,
  kCancelled,
};

constexpr std::string_view
PeriodicTimerStopReasonToString(PeriodicTimerStopReason stop_reason) {
  switch (stop_reason) {
  case PeriodicTimerStopReason::kUnknown:
    return "kUnknown";
  case PeriodicTimerStopReason::kFinished:
    return "kFinished";
  case PeriodicTimerStopReason::kCancelled:
    return "kCancelled";
  }
}

int64_t UptimeInNanoseconds();

class PeriodicTimer {
public:
  struct Parameters {
    // The period in nanoseconds.
    int64_t period_ns = 0;
    absl::AnyInvocable<PeriodicTimerTickResult(
        int64_t /* time_since_timer_start_ns */)>
        tick_callback;
    absl::AnyInvocable<void(PeriodicTimerStopReason) &&> stopped_callback;
  };

  explicit PeriodicTimer(Parameters parameters);
  ~PeriodicTimer();

  // Cancel the timer. This method blocks until the timer is guaranteed to no
  // longer be executing.
  void Cancel();

private:
  Parameters params_;

  dispatch_queue_t queue_;
  dispatch_source_t timer_;
  int64_t start_time_ns_ = 0;

  std::atomic_flag stop_requested_ = ATOMIC_FLAG_INIT;
  std::atomic<PeriodicTimerStopReason> stop_reason_ =
      PeriodicTimerStopReason::kUnknown;
  std::atomic<bool> stopped_ = false;

  void HandleTick();
};

} // namespace fasterswiper
