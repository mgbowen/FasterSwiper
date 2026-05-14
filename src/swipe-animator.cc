#include "src/swipe-animator.h"

#include "src/periodic-timer.h"
#include "src/space-switcher.h"

#include <algorithm>
#include <cmath>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gutil/status.h"
#include "magic_enum/magic_enum.hpp"

namespace fasterswiper {

SwipeAnimator::SwipeAnimator(std::unique_ptr<SpaceSwitchOperation> operation)
    : operation_(std::move(operation)) {}

SwipeAnimator::~SwipeAnimator() {
  (void)CancelAnimation();
  operation_->Commit();
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

absl::Status SwipeAnimator::SetPosition(int64_t new_position) {
  RETURN_IF_ERROR(CancelAnimationAndEnsureNotCommitted());
  operation_->SetPosition(new_position);
  return absl::OkStatus();
}

absl::Status SwipeAnimator::AnimateToPosition(AnimateParameters params) {
  CHECK(params.easing_function != nullptr);

  VLOG(1) << "BEGIN AnimateToPosition(params=" << params << ")";
  auto cleanup = absl::MakeCleanup(
      [&] { VLOG(1) << "END   AnimateToPosition(params=" << params << ")"; });

  if (absl::Status status = CancelAnimationAndEnsureNotCommitted();
      !status.ok()) {
    VLOG(1) << "AnimateToPosition(): animation was already committed";
    return status;
  }

  auto state = std::make_unique<AnimationState>(AnimationState{
      .start_position = operation_->position(),
      .params = std::move(params),
  });

  std::promise<AnimatedSpaceSwitchOperationResult> promise;
  pending_future_ = promise.get_future().share();
  timer_ = std::make_unique<PeriodicTimer>(PeriodicTimer::Parameters{
      .period_ns = 1'000'000'000 / params.ticks_per_second,
      .tick_callback =
          [this, state = std::move(state)](
              int64_t time_since_start_ns) -> PeriodicTimerTickResult {
        const double total_ns = static_cast<double>(
            absl::ToInt64Nanoseconds(state->params.duration));
        const double elapsed_ns = static_cast<double>(time_since_start_ns);
        const double linear_t =
            total_ns <= 0 ? 1.0 : std::clamp(elapsed_ns / total_ns, 0.0, 1.0);
        const double eased_t = state->params.easing_function(linear_t);

        const int64_t interpolated_position =
            state->start_position +
            static_cast<int64_t>(std::round(
                (state->params.target_position - state->start_position) *
                eased_t));

        const bool finished =
            linear_t >= 1.0 ||
            interpolated_position == state->params.target_position;

        operation_->SetPosition(finished ? state->params.target_position
                                         : interpolated_position);

        return finished ? PeriodicTimerTickResult::kFinishTimer
                        : PeriodicTimerTickResult::kContinueTimer;
      },
      .stopped_callback =
          [this, promise = std::move(promise)](
              PeriodicTimerStopReason stop_reason) mutable -> void {
        AnimatedSpaceSwitchOperationResult result;
        switch (stop_reason) {
          using enum PeriodicTimerStopReason;
        case kFinished:
          operation_->Commit();
          result = AnimatedSpaceSwitchOperationResult::kCommitted;
          break;
        case kCancelled:
          result = AnimatedSpaceSwitchOperationResult::kCancelled;
          break;
        }

        promise.set_value(result);
      },
  });

  return absl::OkStatus();
}

AnimatedSpaceSwitchOperationResult SwipeAnimator::CancelAnimation() {
  VLOG(1) << "CancelAnimation(): BEGIN";
  auto cleanup = absl::MakeCleanup([] { VLOG(1) << "CancelAnimation(): END"; });

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

} // namespace fasterswiper
