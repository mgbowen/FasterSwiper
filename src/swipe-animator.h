#pragma once

#include "src/easing.h"
#include "src/periodic-timer.h"
#include "src/space-switcher.h"

#include <future>
#include <memory>

namespace fasterswiper {

enum class AnimatedSpaceSwitchOperationResult {
  kCancelled,
  kCommitted,
};

class SwipeAnimator {
public:
  explicit SwipeAnimator(SpaceState space_state);

  ~SwipeAnimator();

  // Non-copyable, non-movable.
  SwipeAnimator(const SwipeAnimator &) = delete;
  SwipeAnimator &operator=(const SwipeAnimator &) = delete;
  SwipeAnimator(SwipeAnimator &&) = delete;
  SwipeAnimator &operator=(SwipeAnimator &&) = delete;

  // Cancel any active animation and instantly sets the position.
  absl::Status SetPosition(int64_t new_position);

  struct AnimateParameters {
    int64_t target_position ABSL_REQUIRE_EXPLICIT_INIT;
    absl::Duration duration ABSL_REQUIRE_EXPLICIT_INIT;
    EasingFunction easing_function ABSL_REQUIRE_EXPLICIT_INIT;
    int64_t ticks_per_second ABSL_REQUIRE_EXPLICIT_INIT;

    template <typename Sink>
    friend void AbslStringify(Sink &sink, const AnimateParameters &params) {
      absl::Format(&sink,
                   "AnimateParameters{target_position=%d, duration=%s, "
                   "ticks_per_second=%d}",
                   params.target_position, absl::StrCat(params.duration),
                   params.ticks_per_second);
    }
  };

  // Animate from the current position to `target_position` over `duration`.
  // If an animation is already running, it is cancelled (the SpaceSwitcher
  // position is left wherever it currently is) and the new animation begins
  // from there.
  absl::Status AnimateToPosition(AnimateParameters params);

  [[nodiscard]] AnimatedSpaceSwitchOperationResult CancelAnimation();

  [[nodiscard]] const SpaceState &space_state() const {
    return operation_->space_state();
  }

  [[nodiscard]] int64_t position() { return operation_->position(); }

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limit() const {
    return operation_->position_soft_limit();
  }

private:
  const std::unique_ptr<SpaceSwitchOperation> operation_;
  std::unique_ptr<PeriodicTimer> timer_;
  std::shared_future<AnimatedSpaceSwitchOperationResult> pending_future_;

  struct AnimationState {
    int64_t start_position ABSL_REQUIRE_EXPLICIT_INIT;
    AnimateParameters params ABSL_REQUIRE_EXPLICIT_INIT;
  };

  absl::Status CancelAnimationAndEnsureNotCommitted();
};

} // namespace fasterswiper
