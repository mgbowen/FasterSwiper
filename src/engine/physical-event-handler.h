#pragma once

#include "src/channel.h"
#include "src/engine/swipe-animator.h"
#include "src/event.h"
#include "src/hotkeys.h"
#include "src/public/fasterswiper.pb.h"

#include <memory>
#include <thread>

#include <magic_enum/magic_enum.hpp>

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
  struct GestureCommand {
    const DockControlEvent event ABSL_REQUIRE_EXPLICIT_INIT;

    template <typename Sink>
    friend void AbslStringify(Sink &sink, const GestureCommand &command) {
      absl::Format(&sink, "GestureCommand{event=%v}", command.event);
    }
  };

  enum class Direction {
    kLeft,
    kRight,
    kUp,
    kDown,
  };

  struct RelativeMoveCommand {
    const Direction direction ABSL_REQUIRE_EXPLICIT_INIT;

    template <typename Sink>
    friend void AbslStringify(Sink &sink, const RelativeMoveCommand &command) {
      absl::Format(&sink, "RelativeMoveCommand{event=%s}",
                   magic_enum::enum_name(command.direction));
    }
  };

  struct JumpToSpaceCommand {
    const int64_t space_index ABSL_REQUIRE_EXPLICIT_INIT;

    template <typename Sink>
    friend void AbslStringify(Sink &sink, const JumpToSpaceCommand &command) {
      absl::Format(&sink, "JumpToSpaceCommand{space_index=%d}",
                   command.space_index);
    }
  };

  using Command =
      std::variant<GestureCommand, RelativeMoveCommand, JumpToSpaceCommand>;

  const proto::DaemonOptions options_;
  const HotkeyConfigurations hotkey_configs_;

  std::thread event_processor_thread_;
  Channel<Command> channel_{1024};
  std::unique_ptr<SwipeAnimator> animator_;
  int64_t initial_position_ = 0;
  int64_t target_position_ = 0;

  PhysicalEventHandler(proto::DaemonOptions options,
                       HotkeyConfigurations hotkey_configs);

  void EventProcessorThread();

  absl::Status HandleCommand(const GestureCommand &command);
  absl::Status HandleBeginGesture(const DockControlEvent &swipe_event);
  absl::Status HandleChangeGesture(const DockControlEvent &swipe_event);
  absl::Status HandleEndGesture(const DockControlEvent &swipe_event);
  absl::Status HandleCancelGesture(const DockControlEvent &swipe_event);

  absl::Status HandleCommand(const RelativeMoveCommand &command);
  absl::Status HandleCommand(const JumpToSpaceCommand &command);

  absl::Status CheckGestureActive();
  absl::Status SetUpForNewGesture(Axis axis);

  std::optional<RelativeMoveCommand>
  TryGetRelativeMoveCommandFromKeyEvent(const KeyEvent &event) const;
  std::optional<JumpToSpaceCommand>
  TryGetJumpToSpaceCommand(const KeyEvent &event) const;
};

} // namespace fasterswiper
