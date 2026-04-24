#pragma once

#include "src/event.h"
#include "src/swipe-animator.h"
#include "src/channel.h"

#include <memory>
#include <thread>

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

  ~GestureController();

  // Non-copyable, non-movable.
  GestureController(const GestureController &) = delete;
  GestureController &operator=(const GestureController &) = delete;
  GestureController(GestureController &&) = delete;
  GestureController &operator=(GestureController &&) = delete;

  CGEventRef HandleEvent(CGEventTapProxy proxy, CGEventType event_type,
                         CGEventRef event);

private:
  const Options options_;

  std::thread event_processor_thread_;
  Channel<DockSwipeEvent> channel_{1024};
  std::unique_ptr<SwipeAnimator> animator_;
  int64_t initial_position_ = 0;
  int64_t target_position_ = 0;
  std::future<void> active_animation_future_;

  void EventProcessorThread();


  absl::Status HandleBeginGesture();
  absl::Status HandleChangeGesture(const DockSwipeEvent &swipe_event);
  absl::Status HandleEndGesture(const DockSwipeEvent &swipe_event);
  absl::Status HandleCancelGesture(const DockSwipeEvent &swipe_event);
};

} // namespace fasterswiper
