#include "src/engine/swipe-animator.h"

#include "src/engine/space-switch-operation.h"
#include "src/periodic-timer.h"

#include <absl/cleanup/cleanup.h>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/status/status_macros.h>
#include <magic_enum/magic_enum.hpp>

namespace fasterswiper {

SwipeAnimator::SwipeAnimator(std::unique_ptr<SpaceSwitchOperation> operation)
    : operation_(std::move(operation)) {}

SwipeAnimator::~SwipeAnimator() {
  (void)CancelAnimation();

  if (active_event_sink_ == nullptr) {
    active_event_sink_ = std::make_unique<CGEventPostSink>();
  }

  operation_->Commit(active_event_sink_.get());
}

bool SwipeAnimator::is_committed() const {
  if (!pending_future_.valid()) {
    return false;
  }

  if (pending_future_.wait_for(std::chrono::seconds(0)) ==
      std::future_status::timeout) {
    return false;
  }

  return pending_future_.get() ==
         AnimatedSpaceSwitchOperationResult::kCommitted;
}

absl::Status SwipeAnimator::SetPosition(int64_t new_position,
                                        CGEventSink* absl_nonnull event_sink) {
  CHECK(event_sink != nullptr);
  RETURN_IF_ERROR(CancelAnimationAndEnsureNotCommitted());
  operation_->SetPosition(new_position, event_sink);
  return absl::OkStatus();
}

absl::Status SwipeAnimator::AnimateToPosition(
    AnimateParameters params, std::unique_ptr<CGEventSink> event_sink) {
  CHECK(params.easing_function != nullptr);
  CHECK(event_sink != nullptr);

  VLOG(1) << "BEGIN AnimateToPosition(params=" << params << ")";
  absl::Cleanup cleanup = [&] {
    VLOG(1) << "END   AnimateToPosition(params=" << params << ")";
  };

  if (absl::Status status = CancelAnimationAndEnsureNotCommitted();
      !status.ok()) {
    VLOG(1) << "AnimateToPosition(): animation was already committed";
    return status;
  }

  const int64_t period_ns = 1'000'000'000 / params.ticks_per_second;

  auto state = std::make_unique<AnimationState>(AnimationState{
      .start_position = operation_->position(),
      .params = std::move(params),
  });

  active_event_sink_ = std::move(event_sink);

  std::promise<AnimatedSpaceSwitchOperationResult> promise;
  pending_future_ = promise.get_future().share();
  timer_ = std::make_unique<PeriodicTimer>(PeriodicTimer::Parameters{
      .period_ns = period_ns,
      .fire_immediately = operation_->FiresAnimationTimerImmediately(),
      .tick_callback =
          [this, state = std::move(state)](
              int64_t time_since_start_ns) -> PeriodicTimerTickResult {
        CHECK(active_event_sink_ != nullptr);
        const SpaceSwitchOperation::AnimationStepResult result =
            operation_->StepAnimation(
                state->start_position, state->params.target_position,
                time_since_start_ns, state->params.duration,
                state->params.easing_function, active_event_sink_.get());
        return result == SpaceSwitchOperation::AnimationStepResult::kFinished
                   ? PeriodicTimerTickResult::kFinishTimer
                   : PeriodicTimerTickResult::kContinueTimer;
      },
      .stopped_callback =
          [this, promise = std::move(promise)](
              PeriodicTimerStopReason stop_reason) mutable -> void {
        switch (stop_reason) {
          using enum PeriodicTimerStopReason;
          case kFinished:
            CHECK(active_event_sink_ != nullptr);
            operation_->Commit(active_event_sink_.get());
            promise.set_value(AnimatedSpaceSwitchOperationResult::kCommitted);
            break;
          case kCancelled:
            promise.set_value(AnimatedSpaceSwitchOperationResult::kCancelled);
            break;
          default:
            LOG(FATAL)
                << "Unknown PeriodicTimerStopReason "
                << static_cast<std::underlying_type_t<PeriodicTimerStopReason>>(
                       stop_reason);
        }
      },
  });

  return absl::OkStatus();
}

AnimatedSpaceSwitchOperationResult SwipeAnimator::CancelAnimation() {
  VLOG(1) << "CancelAnimation(): BEGIN";
  absl::Cleanup cleanup = [] { VLOG(1) << "CancelAnimation(): END"; };

  if (!pending_future_.valid()) {
    VLOG(1) << "CancelAnimation(): pending_future_ NOT valid";
    return AnimatedSpaceSwitchOperationResult::kCancelled;
  }

  VLOG(1) << "CancelAnimation(): stopping timer";
  timer_.reset();
  VLOG(1) << "CancelAnimation(): timer stopped";

  const AnimatedSpaceSwitchOperationResult result = pending_future_.get();
  VLOG(1) << "CancelAnimation(): timer result="
          << magic_enum::enum_name(result);
  return result;
}

absl::Status SwipeAnimator::CancelAnimationAndEnsureNotCommitted() {
  const AnimatedSpaceSwitchOperationResult cancel_result = CancelAnimation();
  switch (cancel_result) {
    using enum AnimatedSpaceSwitchOperationResult;
    case kCancelled:
      break;
    case kCommitted:
      return absl::FailedPreconditionError(
          "AnimatedSpaceSwitchOperation has already been committed");
  }

  return absl::OkStatus();
}

}  // namespace fasterswiper
