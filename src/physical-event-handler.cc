#include "src/physical-event-handler.h"

#include "src/const.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/space-switch-operation.h"
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
#include <gutil/status.h>
#include <magic_enum/magic_enum.hpp>

namespace fasterswiper {

absl::StatusOr<std::unique_ptr<PhysicalEventHandler>>
PhysicalEventHandler::Create(proto::DaemonOptions options) {
  HotkeyConfigurations hotkey_configs;
  ASSIGN_OR_RETURN(hotkey_configs, LoadHotkeyConfiguration());

  VLOG(1) << "PhysicalEventHandler::Create(): Loaded hotkey configuration: "
          << hotkey_configs;

  auto result = absl::WrapUnique(
      new PhysicalEventHandler(std::move(options), std::move(hotkey_configs)));
  return result;
}

PhysicalEventHandler::PhysicalEventHandler(proto::DaemonOptions options,
                                           HotkeyConfigurations hotkey_configs)
    : options_(std::move(options)), hotkey_configs_(std::move(hotkey_configs)) {
  event_processor_thread_ = std::thread([this] { EventProcessorThread(); });
}

PhysicalEventHandler::~PhysicalEventHandler() {
  channel_.CloseWriter();
  if (event_processor_thread_.joinable()) {
    event_processor_thread_.join();
  }
}

void PhysicalEventHandler::EventProcessorThread() {
  while (true) {
    absl::StatusOr<Event> event = channel_.Read();
    if (!event.ok()) {
      break;
    }

    absl::Status status = std::visit(
        overloaded{[&](const DockControlEvent &dock_control_event) {
                     return HandleDockControlEvent(dock_control_event);
                   },
                   [&](const KeyEvent &key_event) {
                     return HandleKeyEvent(key_event);
                   }},
        event->data);
    if (!status.ok()) {
      LOG(ERROR) << "EventProcessorThread(): Failed to handle event: "
                 << status;
    }
  }
}

CGEventRef PhysicalEventHandler::HandleEvent(CGEventTapProxy proxy,
                                             CGEventType event_type,
                                             CGEventRef event) {
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

  const EventDestination event_destination = std::visit(
      overloaded{[&](const auto &event) { return GetEventDestination(event); }},
      parsed_event->data);
  VLOG(2) << "HandleEvent(): Event destination="
          << magic_enum::enum_name(event_destination);

  switch (event_destination) {
    using enum EventDestination;
  case kHandle:
    VLOG(2) << "HandleEvent(): Handling event";
    break;
  case kPassthrough:
    VLOG(2) << "HandleEvent(): Passing event through";
    return event;
  case kSwallow:
    VLOG(2) << "HandleEvent(): Swallowing event";
    return nullptr;
  }

  absl::Status status = channel_.Write(*std::move(parsed_event));
  if (!status.ok()) {
    LOG(ERROR) << "HandleEvent(): Failed to write to channel: " << status;
  }

  return nullptr;
}

PhysicalEventHandler::EventDestination
PhysicalEventHandler::GetEventDestination(const DockControlEvent &event) const {
  if (animator_ == nullptr || animator_->is_committed()) {
    return EventDestination::kHandle;
  }

  const bool movement_matches_active_animation =
      static_cast<int>(
          animator_->operation().axis_adapter().movement_direction()) ==
      event.direction;
  if (movement_matches_active_animation) {
    return EventDestination::kHandle;
  }

  return EventDestination::kSwallow;
}

PhysicalEventHandler::EventDestination
PhysicalEventHandler::GetEventDestination(const KeyEvent &event) const {
  if (event.key_state != KeyState::kDown) {
    return EventDestination::kPassthrough;
  }

  if (event.ConcernsAnyHotkey(hotkey_configs_)) {
    return EventDestination::kHandle;
  }

  // Check for Control + 1-9
  if ((event.modifiers & kModifierKeyMask) == kCGEventFlagMaskControl) {
    switch (event.key_code) {
    case 18:
    case 19:
    case 20:
    case 21:
    case 23:
    case 22:
    case 26:
    case 28:
    case 25:
      return EventDestination::kHandle;
    }
  }

  return EventDestination::kPassthrough;
}

absl::Status PhysicalEventHandler::HandleDockControlEvent(
    const DockControlEvent &dock_control_event) {
  VLOG(1) << "HandleDockControlEvent(): dock_control_event="
          << dock_control_event;

  if (dock_control_event.phase == kGestureBegan) {
    return HandleBeginGesture(dock_control_event);
  }

  if (dock_control_event.phase == kGestureChanged) {
    return HandleChangeGesture(dock_control_event);
  }

  if (dock_control_event.phase == kGestureCancelled) {
    return HandleCancelGesture(dock_control_event);
  }

  if (dock_control_event.phase == kGestureEnded) {
    return HandleEndGesture(dock_control_event);
  }

  return absl::InternalError(absl::StrCat("Unrecognized DockSwipeEvent phase ",
                                          dock_control_event.phase));
}

absl::Status
PhysicalEventHandler::HandleBeginGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleBeginGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleBeginGesture(): END"; });

  Axis axis;
  switch (swipe_event.direction) {
  case kCGGestureMotionHorizontal:
    axis = Axis::kHorizontal;
    break;
  case kCGGestureMotionVertical:
    axis = Axis::kVertical;
    break;
  default:
    return absl::InvalidArgumentError(absl::StrCat(
        "Unknown DockControlEvent.direction ", swipe_event.direction));
  }

  RETURN_IF_ERROR(SetUpForNewGesture(axis));
  RETURN_IF_ERROR(animator_->SetPosition(initial_position_));
  return absl::OkStatus();
}

absl::Status
PhysicalEventHandler::HandleChangeGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleChangeGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleChangeGesture(): END"; });

  const int64_t new_position =
      initial_position_ +
      animator_->operation().axis_adapter().ProgressToNanoswipes(
          swipe_event.progress);

  VLOG(1) << "HandleChangeGesture():  progress=" << swipe_event.progress;
  VLOG(1) << "HandleChangeGesture():  new_position=" << new_position;

  RETURN_IF_ERROR(animator_->SetPosition(new_position));
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
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleEndGesture(): END"; });

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
  RETURN_IF_ERROR(animator_->AnimateToPosition({
      .target_position = target_position_,
      .duration = duration,
      .easing_function = std::move(easing_function),
      .ticks_per_second = options_.frames_per_second(),
  }));

  return absl::OkStatus();
}

absl::Status
PhysicalEventHandler::HandleCancelGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleCancelGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleCancelGesture(): END"; });

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
  RETURN_IF_ERROR(animator_->AnimateToPosition({
      .target_position = initial_position_,
      .duration = duration,
      .easing_function = std::move(easing_function),
      .ticks_per_second = options_.frames_per_second(),
  }));

  return absl::OkStatus();
}

absl::Status PhysicalEventHandler::HandleKeyEvent(const KeyEvent &key_event) {
  VLOG(1) << "HandleKeyEvent(): key_event=" << key_event;

  Axis axis;
  int64_t direction = 0;
  if (key_event.ConcernsHotkey(hotkey_configs_.move_space_left)) {
    direction = -1;
    axis = Axis::kHorizontal;
  } else if (key_event.ConcernsHotkey(hotkey_configs_.move_space_right)) {
    direction = 1;
    axis = Axis::kHorizontal;
  } else {
    return absl::InternalError("HandleKeyEvent recieved uninteresting event");
  }

  RETURN_IF_ERROR(SetUpForNewGesture(axis));

  const auto [soft_min, soft_max] = animator_->position_soft_limits();
  if (direction != 0) {
    target_position_ =
        std::clamp(((target_position_ / kOneSwipeInNanoswipes) + direction) *
                       kOneSwipeInNanoswipes,
                   soft_min, soft_max);
  } else if ((key_event.modifiers & kModifierKeyMask) ==
             kCGEventFlagMaskControl) {
    std::optional<int> digit;
    switch (key_event.key_code) {
    case 18:
      digit = 0;
      break;
    case 19:
      digit = 1;
      break;
    case 20:
      digit = 2;
      break;
    case 21:
      digit = 3;
      break;
    case 23:
      digit = 4;
      break;
    case 22:
      digit = 5;
      break;
    case 26:
      digit = 6;
      break;
    case 28:
      digit = 7;
      break;
    case 25:
      digit = 8;
      break;
    }

    if (!digit.has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Uninteresting key event ", key_event.key_code));
    }

    const int64_t absolute_target = *digit * kOneSwipeInNanoswipes;
    if (absolute_target > soft_max) {
      VLOG(1) << "Ignoring shortcut: target space " << (*digit + 1)
              << " exceeds available spaces";
      return absl::OkStatus();
    }

    target_position_ = absolute_target;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Uninteresting key event ", key_event.key_code));
  }

  const absl::Duration duration = CalculateAnimationDuration(
      animator_->position(), target_position_,
      FromProtoDuration(options_.animation_duration_per_space()));

  VLOG(1) << "HandleKeyEvent():  initial_position=" << initial_position_;
  VLOG(1) << "HandleKeyEvent():  current_position=" << animator_->position();
  VLOG(1) << "HandleKeyEvent():  target_position=" << target_position_;
  VLOG(1) << "HandleKeyEvent():  duration=" << duration;

  ASSIGN_OR_RETURN(EasingFunction easing_function, FromDaemonOptions(options_));
  RETURN_IF_ERROR(animator_->AnimateToPosition({
      .target_position = target_position_,
      .duration = duration,
      .easing_function = std::move(easing_function),
      .ticks_per_second = options_.frames_per_second(),
  }));

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
      operation = active_window == ActiveMultitaskingWindow::kDesktop
                      ? static_cast<std::unique_ptr<SpaceSwitchOperation>>(
                            std::make_unique<ContinuousSpaceSwitchOperation>(
                                std::move(axis_adapter)))
                      : std::make_unique<SegmentedSpaceSwitchOperation>(
                            std::move(axis_adapter));
      break;
    }
    case kVertical: {
      std::unique_ptr<AxisAdapter> axis_adapter =
          std::make_unique<VerticalAxisAdapter>();
      operation = std::make_unique<SegmentedSpaceSwitchOperation>(
          std::move(axis_adapter));
      break;
    }
    }

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

} // namespace fasterswiper
