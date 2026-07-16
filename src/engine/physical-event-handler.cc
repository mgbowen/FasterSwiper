#include "src/engine/physical-event-handler.h"

#include "src/engine/axis-adapter.h"
#include "src/engine/const.h"
#include "src/engine/space-switch-operation.h"
#include "src/enum-util.h"
#include "src/event.h"
#include "src/hotkeys.h"
#include "src/macos-private.h"
#include "src/mission-control.h"
#include "src/proto-util.h"
#include "src/public/fasterswiper.pb.h"
#include "src/space-state.h"
#include "src/variant-util.h"

#include <algorithm>
#include <memory>
#include <optional>

#include <absl/cleanup/cleanup.h>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/log/vlog_is_on.h>
#include <absl/status/status_macros.h>
#include <magic_enum/magic_enum.hpp>

namespace fasterswiper {

absl::StatusOr<std::unique_ptr<PhysicalEventHandler>>
PhysicalEventHandler::Create(proto::DaemonOptions options) {
  HotkeyConfigurations hotkey_configs{};
  ASSIGN_OR_RETURN(hotkey_configs, LoadHotkeyConfiguration());

  VLOG(1) << "PhysicalEventHandler::Create(): Loaded hotkey configuration: "
          << hotkey_configs;

  auto result = absl::WrapUnique(
      new PhysicalEventHandler(std::move(options), hotkey_configs));
  return result;
}

PhysicalEventHandler::PhysicalEventHandler(proto::DaemonOptions options,
                                           HotkeyConfigurations hotkey_configs)
    : options_(std::move(options)), hotkey_configs_(hotkey_configs) {}

PhysicalEventHandler::~PhysicalEventHandler() {}

namespace {

enum class UninterestingEventDecision {
  kPassthrough,
  kSwallow,
};

}

CGEventRef absl_nullable PhysicalEventHandler::HandleEvent(
    CGEventTapProxy absl_nonnull proxy, CGEventType event_type,
    CGEventRef absl_nonnull event) {
  std::optional<Event> parsed_event = ParseEvent(event);
  if (!parsed_event.has_value()) {
    VLOG(2) << "HandleEvent(): Not a recognized event";
    return event;
  }

  VLOG(2) << "HandleEvent(): parsed_event=" << *parsed_event;

  if (parsed_event->source != EventSource::kPhysical) {
    VLOG(2) << "HandleEvent(): Not a physical event";
    return event;
  }

  VLOG(2) << "HandleEvent(): Received physical event " << *parsed_event;

  using EventDecision = std::variant<UninterestingEventDecision, Command>;

  EventDecision decision =
      std::visit(overloaded{
                     [&](DockControlEvent event) -> EventDecision {
                       return GestureCommand{
                           .event = event,
                       };
                     },
                     [&](KeyEvent event) -> EventDecision {
                       if (event.key_state != KeyState::kDown) {
                         return UninterestingEventDecision::kPassthrough;
                       }

                       if (options_.intercept_mission_control_shortcuts()) {
                         if (std::optional<RelativeMoveCommand> maybe_command =
                                 TryGetRelativeMoveCommandFromKeyEvent(event);
                             maybe_command.has_value()) {
                           return *std::move(maybe_command);
                         }
                       }

                       if (options_.enable_jump_to_space_shortcuts()) {
                         if (std::optional<JumpToSpaceCommand> maybe_command =
                                 TryGetJumpToSpaceCommand(event);
                             maybe_command.has_value()) {
                           return *std::move(maybe_command);
                         }
                       }

                       return UninterestingEventDecision::kPassthrough;
                     },
                 },
                 parsed_event->data);

  return std::visit(
      overloaded{
          [&](UninterestingEventDecision decision) -> CGEventRef {
            VLOG(2) << "HandleEvent(): decision=UninterestingEventDecision::"
                    << magic_enum::enum_name(decision);
            switch (decision) {
              using enum UninterestingEventDecision;
            case kPassthrough:
              return event;
            case kSwallow:
              return nullptr;
            }

            AbortOnUnknownEnum(decision);
          },
          [&](Command command) -> CGEventRef {
            if (VLOG_IS_ON(2)) {
              std::visit(
                  [](const auto &command) {
                    VLOG(2) << "HandleEvent(): decision=" << command;
                  },
                  command);
            }

            absl::Status status = HandleCommand(command, proxy);
            if (!status.ok()) {
              LOG(ERROR) << "HandleEvent(): Failed to handle command: "
                         << status;
              return event;
            }

            return nullptr;
          },
      },
      decision);
}

absl::Status
PhysicalEventHandler::HandleCommand(const Command &command,
                                    CGEventTapProxy absl_nonnull proxy) {
  return std::visit(
      overloaded{
          [&](const GestureCommand &c) { return HandleCommand(c, proxy); },
          [&](const RelativeMoveCommand &c) { return HandleCommand(c); },
          [&](const JumpToSpaceCommand &c) { return HandleCommand(c); }},
      command);
}

absl::Status
PhysicalEventHandler::HandleCommand(const GestureCommand &command,
                                    CGEventTapProxy absl_nonnull proxy) {
  VLOG(1) << "HandleCommand(): command=" << command;

  if (animator_ != nullptr && !animator_->is_committed()) {
    // Active animation, make sure the requested gesture direction matches the
    // animation's direction.
    const Axis active_animation_direction =
        animator_->operation().axis_adapter().movement_direction();
    if (static_cast<int>(active_animation_direction) !=
        command.event.direction) {
      VLOG(1) << "HandleCommand(): Ignoring GestureCommand because an "
                 "animation is active and its direction ("
              << magic_enum::enum_name(active_animation_direction)
              << ") does not match the "
                 "command's requested direction ("
              << magic_enum::enum_name(
                     static_cast<Axis>(command.event.direction))
              << ")";
      return absl::OkStatus();
    }
  }

  if (command.event.phase == kGestureBegan) {
    CGEventTapPostEventSink sink(proxy);
    return HandleBeginGesture(command.event, &sink);
  }

  if (command.event.phase == kGestureChanged) {
    CGEventTapPostEventSink sink(proxy);
    return HandleChangeGesture(command.event, &sink);
  }

  if (command.event.phase == kGestureCancelled) {
    return HandleCancelGesture(command.event);
  }

  if (command.event.phase == kGestureEnded) {
    return HandleEndGesture(command.event);
  }

  return absl::InternalError(
      absl::StrCat("Unrecognized DockSwipeEvent phase ", command.event.phase));
}

absl::Status
PhysicalEventHandler::HandleBeginGesture(const DockControlEvent &swipe_event,
                                         CGEventSink *absl_nonnull event_sink) {
  CHECK(event_sink != nullptr);
  VLOG(1) << "HandleBeginGesture(): BEGIN";
  absl::Cleanup cleanup = [] { VLOG(1) << "HandleBeginGesture(): END"; };

  const Axis axis = [&] {
    switch (swipe_event.direction) {
    case kCGGestureMotionHorizontal:
      return Axis::kHorizontal;
    case kCGGestureMotionVertical:
      return Axis::kVertical;
    default:
      LOG(FATAL) << "Unknown direction " << swipe_event.direction;
    }
  }();

  RETURN_IF_ERROR(SetUpForNewGesture(axis));
  RETURN_IF_ERROR(animator_->SetPosition(initial_position_, event_sink));
  return absl::OkStatus();
}

absl::Status PhysicalEventHandler::HandleChangeGesture(
    const DockControlEvent &swipe_event, CGEventSink *absl_nonnull event_sink) {
  CHECK(event_sink != nullptr);
  VLOG(1) << "HandleChangeGesture(): BEGIN";
  absl::Cleanup cleanup = [] { VLOG(1) << "HandleChangeGesture(): END"; };

  RETURN_IF_ERROR(CheckGestureActive());

  const int64_t new_position =
      initial_position_ +
      animator_->operation().axis_adapter().ProgressToNanoswipes(
          swipe_event.progress);

  VLOG(1) << "HandleChangeGesture():  progress=" << swipe_event.progress;
  VLOG(1) << "HandleChangeGesture():  new_position=" << new_position;

  RETURN_IF_ERROR(animator_->SetPosition(new_position, event_sink));
  return absl::OkStatus();
}

namespace {

absl::Duration
CalculateAnimationDuration(int64_t current_position, int64_t target_position,
                           absl::Duration animation_duration_per_space) {
  const absl::Duration raw_animation_duration =
      animation_duration_per_space *
      (static_cast<double>(std::abs(current_position - target_position)) /
       kOneSwipeInNanoswipes);
  return std::clamp(raw_animation_duration, absl::ZeroDuration(),
                    animation_duration_per_space);
}

} // namespace

absl::Status
PhysicalEventHandler::HandleEndGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleEndGesture(): BEGIN";
  absl::Cleanup cleanup = [] { VLOG(1) << "HandleEndGesture(): END"; };

  RETURN_IF_ERROR(CheckGestureActive());

  const auto [soft_min, soft_max] = animator_->position_soft_limits();
  target_position_ = std::clamp(((target_position_ / kOneSwipeInNanoswipes) +
                                 (swipe_event.progress > 0 ? 1 : -1)) *
                                    kOneSwipeInNanoswipes,
                                soft_min, soft_max);

  const absl::Duration duration = CalculateAnimationDuration(
      animator_->position(), target_position_,
      FromProtoDuration(options_.animation_duration_per_space()));

  VLOG(1) << "HandleEndGesture():  initial_position=" << initial_position_;
  VLOG(1) << "HandleEndGesture():  current_position=" << animator_->position();
  VLOG(1) << "HandleEndGesture():  target_position=" << target_position_;
  VLOG(1) << "HandleEndGesture():  duration=" << duration;

  ASSIGN_OR_RETURN(EasingFunction easing_function, FromDaemonOptions(options_));
  RETURN_IF_ERROR(animator_->AnimateToPosition(
      {
          .target_position = target_position_,
          .duration = duration,
          .easing_function = std::move(easing_function),
          .ticks_per_second = options_.frames_per_second(),
      },
      std::make_unique<CGEventPostSink>()));

  return absl::OkStatus();
}

absl::Status
PhysicalEventHandler::HandleCancelGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleCancelGesture(): BEGIN";
  absl::Cleanup cleanup = [] { VLOG(1) << "HandleCancelGesture(): END"; };

  RETURN_IF_ERROR(CheckGestureActive());

  const absl::Duration duration = CalculateAnimationDuration(
      animator_->position(), target_position_,
      FromProtoDuration(options_.animation_duration_per_space()));

  VLOG(1) << "HandleCancelGesture():  progress=" << swipe_event.progress;
  VLOG(1) << "HandleCancelGesture():  initial_position_=" << initial_position_;
  VLOG(1) << "HandleCancelGesture():  current_position="
          << animator_->position();
  VLOG(1) << "HandleCancelGesture():  target_position_=" << target_position_;
  VLOG(1) << "HandleCancelGesture():  duration=" << duration;

  ASSIGN_OR_RETURN(EasingFunction easing_function, FromDaemonOptions(options_));
  RETURN_IF_ERROR(animator_->AnimateToPosition(
      {
          .target_position = initial_position_,
          .duration = duration,
          .easing_function = std::move(easing_function),
          .ticks_per_second = options_.frames_per_second(),
      },
      std::make_unique<CGEventPostSink>()));

  return absl::OkStatus();
}

absl::Status
PhysicalEventHandler::HandleCommand(const RelativeMoveCommand &command) {
  VLOG(1) << "HandleCommand(): command=" << command;

  const auto [axis, direction_sign] = [&] {
    switch (command.arrow_key_direction) {
      using enum ArrowKeyDirection;
    case kLeft:
      return std::make_pair(Axis::kHorizontal, -1);
    case kRight:
      return std::make_pair(Axis::kHorizontal, 1);
    case kUp:
      return std::make_pair(Axis::kVertical, 1);
    case kDown:
      return std::make_pair(Axis::kVertical, -1);
    }

    AbortOnUnknownEnum(command.arrow_key_direction);
  }();

  RETURN_IF_ERROR(SetUpForNewGesture(axis));

  const auto [soft_min, soft_max] = animator_->position_soft_limits();
  target_position_ =
      std::clamp(((target_position_ / kOneSwipeInNanoswipes) + direction_sign) *
                     kOneSwipeInNanoswipes,
                 soft_min, soft_max);

  const absl::Duration duration = CalculateAnimationDuration(
      animator_->position(), target_position_,
      FromProtoDuration(options_.animation_duration_per_space()));

  VLOG(1) << "HandleKeyEvent():  initial_position=" << initial_position_;
  VLOG(1) << "HandleKeyEvent():  current_position=" << animator_->position();
  VLOG(1) << "HandleKeyEvent():  target_position=" << target_position_;
  VLOG(1) << "HandleKeyEvent():  duration=" << duration;

  ASSIGN_OR_RETURN(EasingFunction easing_function, FromDaemonOptions(options_));
  RETURN_IF_ERROR(animator_->AnimateToPosition(
      {
          .target_position = target_position_,
          .duration = duration,
          .easing_function = std::move(easing_function),
          .ticks_per_second = options_.frames_per_second(),
      },
      std::make_unique<CGEventPostSink>()));

  return absl::OkStatus();
}

absl::Status
PhysicalEventHandler::HandleCommand(const JumpToSpaceCommand &command) {
  VLOG(1) << "HandleCommand(): command=" << command;

  ASSIGN_OR_RETURN(const SpaceState space_state,
                   LoadSpaceStateForActiveDisplay());
  if (command.space_index >= space_state.count()) {
    VLOG(1) << "HandleCommand(): ignoring JumpToSpaceCommand, requested "
               "space_index="
            << command.space_index
            << " is greater than the current number of spaces="
            << space_state.count();
    return absl::OkStatus();
  }

  RETURN_IF_ERROR(SetUpForNewGesture(Axis::kHorizontal));
  target_position_ = command.space_index * kOneSwipeInNanoswipes;

  const absl::Duration duration = CalculateAnimationDuration(
      animator_->position(), target_position_,
      FromProtoDuration(options_.animation_duration_per_space()));

  VLOG(1) << "HandleKeyEvent():  initial_position=" << initial_position_;
  VLOG(1) << "HandleKeyEvent():  current_position=" << animator_->position();
  VLOG(1) << "HandleKeyEvent():  target_position=" << target_position_;
  VLOG(1) << "HandleKeyEvent():  duration=" << duration;

  ASSIGN_OR_RETURN(EasingFunction easing_function, FromDaemonOptions(options_));
  RETURN_IF_ERROR(animator_->AnimateToPosition(
      {
          .target_position = target_position_,
          .duration = duration,
          .easing_function = std::move(easing_function),
          .ticks_per_second = options_.frames_per_second(),
      },
      std::make_unique<CGEventPostSink>()));

  return absl::OkStatus();
}

absl::Status PhysicalEventHandler::CheckGestureActive() {
  if (animator_ == nullptr) {
    return absl::FailedPreconditionError("Gesture is not active");
  }

  return absl::OkStatus();
}

absl::Status PhysicalEventHandler::SetUpForNewGesture(Axis axis) {
  bool need_new_animator = true;
  if (animator_ != nullptr) {
    const AnimatedSpaceSwitchOperationResult cancel_result =
        animator_->CancelAnimation();
    VLOG(1) << "SetUpForNewGesture(): cancel_result="
            << magic_enum::enum_name(cancel_result);

    switch (cancel_result) {
      using enum AnimatedSpaceSwitchOperationResult;
    case kCancelled:
      need_new_animator = false;
      break;
    case kCommitted:
      break;
    }
  }

  if (need_new_animator) {
    std::unique_ptr<SpaceSwitchOperation> operation;

    switch (axis) {
      using enum Axis;
    case kHorizontal: {
      ASSIGN_OR_RETURN(SpaceState space_state,
                       LoadSpaceStateForActiveDisplay());
      ASSIGN_OR_RETURN(const ActiveMultitaskingWindow active_window,
                       GetActiveMultitaskingWindow());

      VLOG(1) << "SetUpForNewGesture(): space_state=" << space_state
              << ", active_window=" << magic_enum::enum_name(active_window);

      std::unique_ptr<AxisAdapter> axis_adapter =
          std::make_unique<HorizontalAxisAdapter>(space_state);
      ASSIGN_OR_RETURN(
          operation,
          active_window == ActiveMultitaskingWindow::kDesktop
              ? static_cast<
                    absl::StatusOr<std::unique_ptr<SpaceSwitchOperation>>>(
                    ContinuousSpaceSwitchOperation::Create(
                        std::move(axis_adapter)))
              : SegmentedSpaceSwitchOperation::Create(std::move(axis_adapter)));
      break;
    }
    case kVertical: {
      std::unique_ptr<AxisAdapter> axis_adapter =
          std::make_unique<VerticalAxisAdapter>();
      ASSIGN_OR_RETURN(operation, SegmentedSpaceSwitchOperation::Create(
                                      std::move(axis_adapter)));
      break;
    }
    }

    VLOG(1) << "SetUpForNewGesture(): axis_adapter.debug_name="
            << operation->axis_adapter().debug_name()
            << ", operation.debug_name=" << operation->debug_name();

    animator_ = std::make_unique<SwipeAnimator>(std::move(operation));

    target_position_ = animator_->position();
  }

  initial_position_ = animator_->position();

  const auto [soft_min, soft_max] = animator_->position_soft_limits();
  VLOG(1) << "SetUpForNewGesture(): need_new_animator=" << need_new_animator
          << ", soft_min=" << soft_min << ", soft_max=" << soft_max
          << ", initial_position_=" << initial_position_
          << ", target_position_=" << target_position_;

  return absl::OkStatus();
}

std::optional<PhysicalEventHandler::RelativeMoveCommand>
PhysicalEventHandler::TryGetRelativeMoveCommandFromKeyEvent(
    const KeyEvent &event) const {
  if (event.ConcernsHotkey(hotkey_configs_.move_space_left)) {
    return RelativeMoveCommand{
        .arrow_key_direction = ArrowKeyDirection::kLeft,
    };
  }

  if (event.ConcernsHotkey(hotkey_configs_.move_space_right)) {
    return RelativeMoveCommand{
        .arrow_key_direction = ArrowKeyDirection::kRight,
    };
  }

  if (event.ConcernsHotkey(hotkey_configs_.open_mission_control)) {
    return RelativeMoveCommand{
        .arrow_key_direction = ArrowKeyDirection::kUp,
    };
  }

  if (event.ConcernsHotkey(hotkey_configs_.open_app_expose)) {
    return RelativeMoveCommand{
        .arrow_key_direction = ArrowKeyDirection::kDown,
    };
  }

  return std::nullopt;
}

std::optional<PhysicalEventHandler::JumpToSpaceCommand>
PhysicalEventHandler::TryGetJumpToSpaceCommand(const KeyEvent &event) const {
  if ((event.modifiers & kModifierKeyMask) != kCGEventFlagMaskControl) {
    return std::nullopt;
  }

  auto space_index = [&]() -> std::optional<int64_t> {
    switch (event.key_code) {
    case 18:
      return 0;
    case 19:
      return 1;
    case 20:
      return 2;
    case 21:
      return 3;
    case 23:
      return 4;
    case 22:
      return 5;
    case 26:
      return 6;
    case 28:
      return 7;
    case 25:
      return 8;
    case 29:
      return 9;
    default:
      return std::nullopt;
    }
  }();

  if (!space_index.has_value()) {
    return std::nullopt;
  }

  return JumpToSpaceCommand{
      .space_index = *space_index,
  };
}

} // namespace fasterswiper
