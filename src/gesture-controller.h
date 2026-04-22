#pragma once

#include "src/event.h"
#include "src/swipe-animator.h"

#include <memory>

namespace fasterswiper {

class GestureController {
public:
  struct Options {
    absl::Duration animation_duration_per_space = absl::Milliseconds(200);
    EasingFunctionType easing_function_type =
        EasingFunctionType::kEasingFunctionEaseOutQuadratic;
    int64_t ticks_per_second = 240;
  };

  GestureController() : GestureController(Options{}) {}
  explicit GestureController(Options options);

  ~GestureController() = default;

  // Non-copyable, non-movable.
  GestureController(const GestureController &) = delete;
  GestureController &operator=(const GestureController &) = delete;
  GestureController(GestureController &&) = delete;
  GestureController &operator=(GestureController &&) = delete;

  CGEventRef HandleEvent(CGEventTapProxy proxy, CGEventType event_type,
                         CGEventRef event);

private:
  const Options options_;

  std::unique_ptr<SwipeAnimator> animator_;
  int64_t initial_position_ = 0;
  int64_t target_position_ = 0;
  std::future<void> active_animation_future_;

  absl::StatusOr<CGEventRef> TryHandleEvent(CGEventTapProxy proxy,
                                            CGEventType event_type,
                                            CGEventRef event);
  absl::Status HandleBeginGesture();
  absl::Status HandleChangeGesture(const DockSwipeEvent &swipe_event);
  absl::Status HandleEndGesture(const DockSwipeEvent &swipe_event);
  absl::Status HandleCancelGesture(const DockSwipeEvent &swipe_event);
};

} // namespace fasterswiper
