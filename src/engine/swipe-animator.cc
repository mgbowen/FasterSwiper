#include "src/engine/swipe-animator.h"

#include "src/animation-timer.h"
#include "src/engine/space-switch-operation.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

#include <absl/cleanup/cleanup.h>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/time/time.h>
#include <magic_enum/magic_enum.hpp>

namespace fasterswiper {

SwipeAnimator::SwipeAnimator(std::unique_ptr<SpaceSwitchOperation> operation,
                             std::optional<CGDirectDisplayID> display_id,
                             AnimationTimerFactory timer_factory)
    : operation_(std::move(operation)),
      display_id_(display_id),
      timer_factory_(std::move(timer_factory)) {
  CHECK(operation_ != nullptr);
  CHECK(timer_factory_ != nullptr);
}

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

  const absl::Duration duration = params.duration;

  auto state = std::make_unique<AnimationState>(AnimationState{
      .start_position = operation_->position(),
      .params = std::move(params),
  });

  active_event_sink_ = std::move(event_sink);

  std::promise<AnimatedSpaceSwitchOperationResult> promise;
  pending_future_ = promise.get_future().share();
  AnimationTimerParameters timer_params{
      .display_id = display_id_,
      .duration = duration,
      .fire_immediately = operation_->FiresAnimationTimerImmediately() ||
                          duration <= absl::ZeroDuration(),
      .tick_callback = [this, state = std::move(state)](
                           int64_t time_since_start_ns) -> AnimationTickResult {
        CHECK(active_event_sink_ != nullptr);
        const SpaceSwitchOperation::AnimationStepResult result =
            operation_->StepAnimation(
                state->start_position, state->params.target_position,
                time_since_start_ns, state->params.duration,
                state->params.easing_function, active_event_sink_.get());
        return result == SpaceSwitchOperation::AnimationStepResult::kFinished
                   ? AnimationTickResult::kFinish
                   : AnimationTickResult::kContinue;
      },
      .stopped_callback = [this, promise = std::move(promise)](
                              AnimationStopReason stop_reason) mutable -> void {
        switch (stop_reason) {
          using enum AnimationStopReason;
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
                << "Unknown AnimationStopReason "
                << static_cast<std::underlying_type_t<AnimationStopReason>>(
                       stop_reason);
        }
      },
  };

  timer_ = timer_factory_(std::move(timer_params));
  CHECK(timer_ != nullptr);

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
