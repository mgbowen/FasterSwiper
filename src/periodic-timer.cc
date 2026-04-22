#include "src/periodic-timer.h"

#include <iostream>

#include "absl/log/log.h"

namespace fasterswiper {

int64_t UptimeInNanoseconds() {
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

PeriodicTimer::PeriodicTimer(Parameters parameters)
    : params_(std::move(parameters)) {
  if (params_.period_ns == 0) {
    LOG(FATAL) << "ERROR: period_ns must be > 0";
  }

  dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
      DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
  queue_ = dispatch_queue_create("com.fasterswiper.PeriodicTimer", attr);

  timer_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue_);

  dispatch_source_set_event_handler(timer_, ^{
    HandleTick();
  });

  dispatch_source_set_cancel_handler(timer_, ^{
    if (!stop_requested_.test_and_set()) {
      LOG(FATAL) << "Timer cancelled without stop_requested_ being set!";
    }

    std::move(params_.stopped_callback)(stop_reason_);

    stopped_ = true;
    stopped_.notify_all();
  });

  dispatch_source_set_timer(timer_,
                            dispatch_time(DISPATCH_TIME_NOW, params_.period_ns),
                            params_.period_ns, 0);

  start_time_ns_ = UptimeInNanoseconds();
  dispatch_resume(timer_);
}

PeriodicTimer::~PeriodicTimer() {
  Cancel();
  dispatch_release(timer_);
  dispatch_release(queue_);
}

void PeriodicTimer::Cancel() {
  if (!stop_requested_.test_and_set()) {
    stop_reason_ = PeriodicTimerStopReason::kCancelled;
    dispatch_source_cancel(timer_);
  }

  stopped_.wait(false);
}

void PeriodicTimer::HandleTick() {
  if (stop_requested_.test()) {
    return;
  }

  if (params_.tick_callback(UptimeInNanoseconds() - start_time_ns_) ==
      PeriodicTimerTickResult::kFinishTimer) {
    if (!stop_requested_.test_and_set()) {
      stop_reason_ = PeriodicTimerStopReason::kFinished;
      dispatch_source_cancel(timer_);
    }
  }
}

} // namespace fasterswiper
