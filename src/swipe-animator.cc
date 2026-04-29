#include "src/swipe-animator.h"

#include <algorithm>
#include <cmath>

#include "absl/log/check.h"

namespace fasterswiper {

SwipeAnimator::SwipeAnimator(
    absl_nonnull std::unique_ptr<SpaceSwitcher> space_switcher)
    : switcher_(std::move(space_switcher)) {
  CHECK(switcher_ != nullptr);
}

SwipeAnimator::~SwipeAnimator() { CancelAnimation(); }

void SwipeAnimator::SetPosition(int64_t new_position) {
  CancelAnimation();
  switcher_->SetPosition(new_position);
}

void SwipeAnimator::WaitForPendingCommit() {
  switcher_->WaitForPendingCommit();
}

std::future<void> SwipeAnimator::AnimateToPosition(AnimateParameters params) {
  CHECK(params.easing_function != nullptr);

  CancelAnimation();

  auto state = std::make_unique<AnimationState>(AnimationState{
      .start_position = switcher_->position(),
      .params = std::move(params),
  });

  std::promise<void> promise;
  std::future<void> future = promise.get_future();
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

        const bool finished = linear_t >= 1.0;

        switcher_->SetPosition(finished ? state->params.target_position
                                        : interpolated_position);
        if (finished) {
          switcher_->WaitForPendingCommit();
          return PeriodicTimerTickResult::kFinishTimer;
        }

        return PeriodicTimerTickResult::kContinueTimer;
      },
      .stopped_callback =
          [promise = std::move(promise)](
              PeriodicTimerStopReason stop_reason) mutable {
            promise.set_value();
          },
  });

  return future;
}

void SwipeAnimator::CancelAnimation() {
  timer_.reset();
  switcher_->WaitForPendingCommit();
}

} // namespace fasterswiper
