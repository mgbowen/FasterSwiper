#pragma once

#include "src/easing-functions.h"
#include "src/periodic-timer.h"
#include "src/space-switcher.h"

#include <future>
#include <memory>

namespace fasterswiper {

class SwipeAnimator {
public:
  explicit SwipeAnimator(
      absl_nonnull std::unique_ptr<SpaceSwitcher> space_switcher);

  ~SwipeAnimator();

  // Non-copyable, non-movable.
  SwipeAnimator(const SwipeAnimator &) = delete;
  SwipeAnimator &operator=(const SwipeAnimator &) = delete;
  SwipeAnimator(SwipeAnimator &&) = delete;
  SwipeAnimator &operator=(SwipeAnimator &&) = delete;

  // Cancel any active animation and instantly sets the position.
  void SetPosition(int64_t new_position);

  void WaitForPendingCommit();

  struct AnimateParameters {
    int64_t target_position ABSL_REQUIRE_EXPLICIT_INIT;
    absl::Duration duration ABSL_REQUIRE_EXPLICIT_INIT;
    absl_nonnull EasingFunctionPtr easing_function ABSL_REQUIRE_EXPLICIT_INIT;
    int64_t ticks_per_second ABSL_REQUIRE_EXPLICIT_INIT;
  };

  // Animate from the current position to `target_position` over `duration`.
  // If an animation is already running, it is cancelled (the SpaceSwitcher
  // position is left wherever it currently is) and the new animation begins
  // from there.
  [[nodiscard]] std::future<void> AnimateToPosition(AnimateParameters params);

  void CancelAnimation();

  [[nodiscard]] const SpaceSwitcher &space_switcher() const {
    return *switcher_;
  }

  [[nodiscard]] const SpaceState &space_state() const {
    return switcher_->space_state();
  }

  [[nodiscard]] int64_t position() { return switcher_->position(); }

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limit() const {
    return switcher_->position_soft_limit();
  }

private:
  const std::unique_ptr<SpaceSwitcher> switcher_;
  std::unique_ptr<PeriodicTimer> timer_;

  struct AnimationState {
    int64_t start_position ABSL_REQUIRE_EXPLICIT_INIT;
    AnimateParameters params ABSL_REQUIRE_EXPLICIT_INIT;
  };
};

} // namespace fasterswiper
