#pragma once

#include "src/channel.h"
#include "src/event.h"
#include "src/hotkeys.h"
#include "src/public/fasterswiper.pb.h"
#include "src/swipe-animator.h"

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

  const absl::Duration animation_duration_per_space_;

  std::thread event_processor_thread_;
  Channel<Event> channel_{1024};
  std::unique_ptr<SwipeAnimator> animator_;
  int64_t initial_position_ = 0;
  int64_t target_position_ = 0;

  PhysicalEventHandler(proto::DaemonOptions options,
                       HotkeyConfigurations hotkey_configs);

  void EventProcessorThread();

  bool IsEventInteresting(const DockControlEvent &event) const;
  bool IsEventInteresting(const KeyEvent &event) const;

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
