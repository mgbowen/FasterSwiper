#include "src/physical-event-handler.h"

#include "src/const.h"
#include "src/event.h"
#include "src/hotkeys.h"
#include "src/macos-private.h"
#include "src/notifications.h"
#include "src/proto-util.h"
#include "src/public/fasterswiper.pb.h"
#include "src/space-state.h"
#include "src/status-macros.h"
#include "src/variant-util.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"

namespace fasterswiper {

absl::StatusOr<std::unique_ptr<PhysicalEventHandler>>
PhysicalEventHandler::Create(
    proto::DaemonOptions options,
    std::unique_ptr<NotificationManager> notification_manager) {
  HotkeyConfigurations hotkey_configs;
  ASSIGN_OR_RETURN(hotkey_configs, LoadHotkeyConfiguration());

  VLOG(1) << "PhysicalEventHandler::Create(): Loaded hotkey configuration: "
          << hotkey_configs;

  auto result = absl::WrapUnique(new PhysicalEventHandler(
      std::move(options), std::move(notification_manager),
      std::move(hotkey_configs)));
  return result;
}

PhysicalEventHandler::PhysicalEventHandler(
    proto::DaemonOptions options,
    std::unique_ptr<NotificationManager> notification_manager,
    HotkeyConfigurations hotkey_configs)
    : options_(std::move(options)),
      notification_manager_(std::move(notification_manager)),
      hotkey_configs_(std::move(hotkey_configs)) {
  CHECK(notification_manager_ != nullptr);

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

  if (parsed_event->source != EventSource::kPhysical) {
    VLOG(2) << "HandleEvent(): Not a physical event";
    return event;
  }

  VLOG(2) << "HandleEvent(): Received physical event " << *parsed_event;

  const bool is_event_interesting = std::visit(
      overloaded{[&](const auto &event) { return IsEventInteresting(event); }},
      parsed_event->data);
  if (!is_event_interesting) {
    VLOG(2) << "HandleEvent(): Not an interesting event";
    return event;
  }

  absl::Status status = channel_.Write(*std::move(parsed_event));
  if (!status.ok()) {
    LOG(ERROR) << "HandleEvent(): Failed to write to channel: " << status;
  }

  return nullptr;
}

bool PhysicalEventHandler::IsEventInteresting(
    const DockControlEvent &event) const {
  return event.direction == kCGGestureMotionHorizontal;
}

bool PhysicalEventHandler::IsEventInteresting(const KeyEvent &event) const {
  if (event.key_state != KeyState::kDown) {
    return false;
  }

  if (event.ConcernsAnyHotkey(hotkey_configs_)) {
    return true;
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
      return true;
    }
  }

  return false;
}

absl::Status PhysicalEventHandler::HandleDockControlEvent(
    const DockControlEvent &dock_control_event) {
  VLOG(1) << "HandleDockControlEvent(): dock_control_event="
          << dock_control_event;

  if (dock_control_event.phase == kGestureBegan) {
    return HandleBeginGesture();
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

absl::Status PhysicalEventHandler::HandleBeginGesture() {
  VLOG(1) << "HandleBeginGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleBeginGesture(): END"; });

  RETURN_IF_ERROR(SetUpForNewGesture());

  animator_->SetPosition(initial_position_,
                         {.wait_for_space_transition = false});
  return absl::OkStatus();
}

absl::Status
PhysicalEventHandler::HandleChangeGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleChangeGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleChangeGesture(): END"; });

  const int64_t new_position =
      initial_position_ +
      animator_->space_state().ProgressToSwipes(swipe_event.progress);

  VLOG(1) << "HandleChangeGesture():  progress=" << swipe_event.progress;
  VLOG(1) << "HandleChangeGesture():  new_position=" << new_position;

  animator_->SetPosition(new_position, {.wait_for_space_transition = false});
  return absl::OkStatus();
}

namespace {

absl::Duration
CalculateAnimationDuration(int64_t current_position, int64_t target_position,
                           absl::Duration animation_duration_per_space) {
  const absl::Duration raw_animation_duration =
      animation_duration_per_space *
      (static_cast<double>(std::abs(current_position - target_position)) /
       OneSwipeInNanoswipes);
  return std::clamp(raw_animation_duration, absl::ZeroDuration(),
                    animation_duration_per_space);
}

} // namespace

absl::Status
PhysicalEventHandler::HandleEndGesture(const DockControlEvent &swipe_event) {
  VLOG(1) << "HandleEndGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { VLOG(1) << "HandleEndGesture(): END"; });

  const auto [soft_min, soft_max] = animator_->position_soft_limit();
  target_position_ = std::clamp(((target_position_ / OneSwipeInNanoswipes) +
                                 (swipe_event.progress > 0 ? 1 : -1)) *
                                    OneSwipeInNanoswipes,
                                soft_min, soft_max);

  const absl::Duration duration = CalculateAnimationDuration(
      animator_->position(), target_position_,
      FromProtoDuration(options_.animation_duration_per_space()));

  VLOG(1) << "HandleEndGesture():  initial_position=" << initial_position_;
  VLOG(1) << "HandleEndGesture():  current_position=" << animator_->position();
  VLOG(1) << "HandleEndGesture():  target_position=" << target_position_;
  VLOG(1) << "HandleEndGesture():  duration=" << duration;

  ASSIGN_OR_RETURN(EasingFunction easing_function, FromDaemonOptions(options_));

  active_animation_future_ = animator_->AnimateToPosition({
      .target_position = target_position_,
      .duration = duration,
      .easing_function = std::move(easing_function),
      .ticks_per_second = options_.frames_per_second(),
  });

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

  active_animation_future_ = animator_->AnimateToPosition({
      .target_position = initial_position_,
      .duration = duration,
      .easing_function = std::move(easing_function),
      .ticks_per_second = options_.frames_per_second(),
  });

  return absl::OkStatus();
}

absl::Status PhysicalEventHandler::HandleKeyEvent(const KeyEvent &key_event) {
  VLOG(1) << "HandleKeyEvent(): key_event=" << key_event;

  RETURN_IF_ERROR(SetUpForNewGesture());

  int64_t direction = 0;
  if (key_event.ConcernsHotkey(hotkey_configs_.move_space_left)) {
    direction = -1;
  } else if (key_event.ConcernsHotkey(hotkey_configs_.move_space_right)) {
    direction = 1;
  }

  const auto [soft_min, soft_max] = animator_->position_soft_limit();
  if (direction != 0) {
    target_position_ =
        std::clamp(((target_position_ / OneSwipeInNanoswipes) + direction) *
                       OneSwipeInNanoswipes,
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

    const int64_t absolute_target = *digit * OneSwipeInNanoswipes;
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

  active_animation_future_ = animator_->AnimateToPosition({
      .target_position = target_position_,
      .duration = duration,
      .easing_function = std::move(easing_function),
      .ticks_per_second = options_.frames_per_second(),
  });

  return absl::OkStatus();
}

absl::Status PhysicalEventHandler::SetUpForNewGesture() {
  const bool is_active_animation =
      animator_ != nullptr && active_animation_future_.valid() &&
      active_animation_future_.wait_for(
          std::chrono::steady_clock::duration::zero()) !=
          std::future_status::ready;

  if (is_active_animation) {
    animator_->CancelAnimation();
    active_animation_future_.wait();
  } else {
    ASSIGN_OR_RETURN(SpaceState space_state, LoadSpaceStateForActiveDisplay());
    animator_ = std::make_unique<SwipeAnimator>(std::move(space_state));

    target_position_ = animator_->position();
  }

  initial_position_ = animator_->position();

  const auto [soft_min, soft_max] = animator_->position_soft_limit();
  VLOG(1) << "SetUpForNewGesture():   is_active_animation="
          << is_active_animation << ", space_state=" << animator_->space_state()
          << ", soft_min=" << soft_min << ", soft_max=" << soft_max
          << ", initial_position_=" << initial_position_
          << ", target_position_=" << target_position_;

  return absl::OkStatus();
}

} // namespace fasterswiper
