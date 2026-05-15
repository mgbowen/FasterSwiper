#pragma once

#include "src/channel.h"
#include "src/engine/swipe-animator.h"
#include "src/event.h"
#include "src/hotkeys.h"
#include "src/public/fasterswiper.pb.h"

#include <memory>
#include <thread>

namespace fasterswiper {

class PhysicalEventHandler {
public:
  static absl::StatusOr<std::unique_ptr<PhysicalEventHandler>>
  Create(proto::DaemonOptions options);

  ~PhysicalEventHandler();

  // Non-copyable, non-movable.
  PhysicalEventHandler(const PhysicalEventHandler &) = delete;
  PhysicalEventHandler &operator=(const PhysicalEventHandler &) = delete;
  PhysicalEventHandler(PhysicalEventHandler &&) = delete;
  PhysicalEventHandler &operator=(PhysicalEventHandler &&) = delete;

  CGEventRef HandleEvent(CGEventTapProxy proxy, CGEventType event_type,
                         CGEventRef event);

private:
  const proto::DaemonOptions options_;
  const HotkeyConfigurations hotkey_configs_;

  std::thread event_processor_thread_;
  Channel<Event> channel_{1024};
  std::unique_ptr<SwipeAnimator> animator_;
  int64_t initial_position_ = 0;
  int64_t target_position_ = 0;

  PhysicalEventHandler(proto::DaemonOptions options,
                       HotkeyConfigurations hotkey_configs);

  void EventProcessorThread();

  enum class EventDestination {
    kPassthrough,
    kSwallow,
    kHandle,
  };

  EventDestination GetEventDestination(const DockControlEvent &event) const;
  EventDestination GetEventDestination(const KeyEvent &event) const;

  absl::Status
  HandleDockControlEvent(const DockControlEvent &dock_control_event);
  absl::Status HandleBeginGesture(const DockControlEvent &swipe_event);
  absl::Status HandleChangeGesture(const DockControlEvent &swipe_event);
  absl::Status HandleEndGesture(const DockControlEvent &swipe_event);
  absl::Status HandleCancelGesture(const DockControlEvent &swipe_event);

  absl::Status HandleKeyEvent(const KeyEvent &key_event);

  absl::Status CheckGestureActive();
  absl::Status SetUpForNewGesture(Axis axis);
};

} // namespace fasterswiper
