#pragma once

#include "src/channel.h"
#include "src/event.h"
#include "src/swipe-animator.h"

#include <memory>
#include <thread>

namespace fasterswiper {

class PhysicalEventHandler {
public:
  struct Options {
    absl::Duration animation_duration_per_space = absl::Milliseconds(200);
    EasingFunctionType easing_function_type =
        EasingFunctionType::kEasingFunctionEaseOutQuadratic;
    int64_t ticks_per_second = 240;
    bool handle_keyboard_events = true;
  };

  PhysicalEventHandler() : PhysicalEventHandler(Options{}) {}
  explicit PhysicalEventHandler(Options options);

  ~PhysicalEventHandler();

  // Non-copyable, non-movable.
  PhysicalEventHandler(const PhysicalEventHandler &) = delete;
  PhysicalEventHandler &operator=(const PhysicalEventHandler &) = delete;
  PhysicalEventHandler(PhysicalEventHandler &&) = delete;
  PhysicalEventHandler &operator=(PhysicalEventHandler &&) = delete;

  CGEventRef HandleEvent(CGEventTapProxy proxy, CGEventType event_type,
                         CGEventRef event);

private:
  const Options options_;

  std::thread event_processor_thread_;
  Channel<Event> channel_{1024};
  std::unique_ptr<SwipeAnimator> animator_;
  int64_t initial_position_ = 0;
  int64_t target_position_ = 0;
  std::future<void> active_animation_future_;

  void EventProcessorThread();

  absl::Status
  HandleDockControlEvent(const DockControlEvent &dock_control_event);
  absl::Status HandleBeginGesture();
  absl::Status HandleChangeGesture(const DockControlEvent &swipe_event);
  absl::Status HandleEndGesture(const DockControlEvent &swipe_event);
  absl::Status HandleCancelGesture(const DockControlEvent &swipe_event);

  absl::Status HandleKeyEvent(const KeyEvent &key_event);

  absl::Status SetUpForNewGesture();
};

} // namespace fasterswiper
