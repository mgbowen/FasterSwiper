#include "src/periodic-timer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include <absl/log/log.h>
#include <absl/time/time.h>

namespace fasterswiper {

namespace {

constexpr int64_t kDefaultFallbackTicksPerSecond = 240;

}  // namespace

int64_t UptimeInNanoseconds() {
  return static_cast<int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}

PeriodicTimer::PeriodicTimer(Parameters parameters)
    : params_(std::move(parameters)) {
  if (params_.period_ns <= 0) {
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

  const dispatch_time_t start_time =
      params_.fire_immediately
          ? DISPATCH_TIME_NOW
          : dispatch_time(DISPATCH_TIME_NOW, params_.period_ns);
  dispatch_source_set_timer(timer_, start_time, params_.period_ns, 0);

  start_time_ns_ = UptimeInNanoseconds();
  dispatch_resume(timer_);
}

PeriodicTimer::~PeriodicTimer() {
  CancelImpl();
  dispatch_release(timer_);
  dispatch_release(queue_);
}

void PeriodicTimer::Cancel() { CancelImpl(); }

void PeriodicTimer::CancelImpl() {
  if (!stop_requested_.test_and_set()) {
    stop_reason_ = AnimationStopReason::kCancelled;
    dispatch_source_cancel(timer_);
  }

  stopped_.wait(false);
}

void PeriodicTimer::HandleTick() {
  if (stop_requested_.test()) {
    return;
  }

  if (params_.tick_callback(UptimeInNanoseconds() - start_time_ns_) ==
      AnimationTickResult::kFinish) {
    if (!stop_requested_.test_and_set()) {
      stop_reason_ = AnimationStopReason::kFinished;
      dispatch_source_cancel(timer_);
    }
  }
}

AnimationTimerFactory MakePeriodicTimerFactory(
    std::optional<int64_t> ticks_per_second_override) {
  return [ticks_per_second_override](AnimationTimerParameters params)
             -> std::unique_ptr<AnimationTimer> {
    const int64_t requested_ticks_per_second =
        ticks_per_second_override.value_or(params.fallback_ticks_per_second);
    const int64_t ticks_per_second = requested_ticks_per_second > 0
                                         ? requested_ticks_per_second
                                         : kDefaultFallbackTicksPerSecond;
    const int64_t period_ns =
        std::max<int64_t>(1, 1'000'000'000LL / ticks_per_second);
    return std::make_unique<PeriodicTimer>(PeriodicTimer::Parameters{
        .period_ns = period_ns,
        .fire_immediately =
            params.fire_immediately || params.duration <= absl::ZeroDuration(),
        .tick_callback = std::move(params.tick_callback),
        .stopped_callback = std::move(params.stopped_callback),
    });
  };
}

}  // namespace fasterswiper
