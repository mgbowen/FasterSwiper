#include "src/gesture-controller.h"
#include "src/const.h"
#include "src/easing-functions.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/space-state.h"
#include "src/status-macros.h"

#include <algorithm>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"

namespace fasterswiper {

GestureController::GestureController(Options options)
    : options_(std::move(options)) {
  CHECK(GetEasingFunction(options_.easing_function_type) != nullptr)
      << "Invalid easing function type: " << options_.easing_function_type;
}

CGEventRef GestureController::HandleEvent(CGEventTapProxy proxy,
                                          CGEventType event_type,
                                          CGEventRef event) {
  DVLOG(2) << "HandleEvent() BEGIN";
  absl::StatusOr<CGEventRef> result = TryHandleEvent(proxy, event_type, event);
  auto cleanup = absl::MakeCleanup([] { DVLOG(2) << "HandleEvent() END"; });

  if (!result.ok()) {
    LOG(ERROR) << "HandleEvent(): Failed to handle event: " << result.status();
    return event;
  }

  return result.value();
}

absl::StatusOr<CGEventRef>
GestureController::TryHandleEvent(CGEventTapProxy proxy, CGEventType event_type,
                                  CGEventRef event) {
  std::optional<DockSwipeEvent> swipe_event = ParseDockSwipeEvent(event);
  if (!swipe_event) {
    DVLOG(2) << "TryHandleEvent():  Not a swipe event";
    return nullptr;
  }

  if (swipe_event->source != DockSwipeEventSource::kPhysical) {
    DVLOG(2) << "TryHandleEvent():  Not a physical swipe";
    return event;
  }

  if (swipe_event->direction != kCGGestureMotionHorizontal) {
    DVLOG(2) << "TryHandleEvent():  Not a horizontal swipe";
    return event;
  }

  DVLOG(1) << "TryHandleEvent():  BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { DVLOG(1) << "TryHandleEvent():  END"; });

  if (swipe_event->phase == kGestureBegan) {
    RETURN_IF_ERROR(HandleBeginGesture());
  } else if (swipe_event->phase == kGestureChanged) {
    RETURN_IF_ERROR(HandleChangeGesture(*swipe_event));
  } else if (swipe_event->phase == kGestureCancelled) {
    RETURN_IF_ERROR(HandleCancelGesture(*swipe_event));
  } else if (swipe_event->phase == kGestureEnded) {
    RETURN_IF_ERROR(HandleEndGesture(*swipe_event));
  }

  return nullptr;
}

absl::Status GestureController::HandleBeginGesture() {
  DVLOG(1) << "HandleBeginGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { DVLOG(1) << "HandleBeginGesture(): END"; });

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
    auto switcher = std::make_unique<SpaceSwitcher>(std::move(space_state));
    animator_ = std::make_unique<SwipeAnimator>(std::move(switcher));

    target_position_ = animator_->position();
  }

  initial_position_ = animator_->position();

  DVLOG(1) << "HandleBeginGesture():   is_active_animation="
           << is_active_animation;
  DVLOG(1) << "HandleBeginGesture():   space_state="
           << animator_->space_state();

  {
    const auto [soft_min, soft_max] = animator_->position_soft_limit();
    DVLOG(1) << "HandleBeginGesture():   soft_min=" << soft_min
             << ", soft_max=" << soft_max;
  }

  DVLOG(1) << "HandleBeginGesture():   initial_position_=" << initial_position_;
  DVLOG(1) << "HandleBeginGesture():   target_position_=" << target_position_;

  animator_->SetPosition(initial_position_);
  return absl::OkStatus();
}

absl::Status
GestureController::HandleChangeGesture(const DockSwipeEvent &swipe_event) {
  DVLOG(1) << "HandleChangeGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { DVLOG(1) << "HandleChangeGesture(): END"; });

  const int64_t new_position =
      initial_position_ +
      animator_->space_state().ProgressToSwipes(swipe_event.progress);

  DVLOG(1) << "HandleChangeGesture():  progress=" << swipe_event.progress;
  DVLOG(1) << "HandleChangeGesture():  new_position=" << new_position;

  animator_->SetPosition(new_position);
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
GestureController::HandleEndGesture(const DockSwipeEvent &swipe_event) {
  DVLOG(1) << "HandleEndGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { DVLOG(1) << "HandleEndGesture(): END"; });

  const auto [soft_min, soft_max] = animator_->position_soft_limit();
  target_position_ = std::clamp(((target_position_ / OneSwipeInNanoswipes) +
                                 (swipe_event.progress > 0 ? 1 : -1)) *
                                    OneSwipeInNanoswipes,
                                soft_min, soft_max);

  const absl::Duration duration =
      CalculateAnimationDuration(animator_->position(), target_position_,
                                 options_.animation_duration_per_space);

  DVLOG(1) << "HandleEndGesture():  initial_position=" << initial_position_;
  DVLOG(1) << "HandleEndGesture():  current_position=" << animator_->position();
  DVLOG(1) << "HandleEndGesture():  target_position=" << target_position_;
  DVLOG(1) << "HandleEndGesture():  duration=" << duration;

  active_animation_future_ = animator_->AnimateToPosition({
      .target_position = target_position_,
      .duration = duration,
      .easing_function = GetEasingFunction(options_.easing_function_type),
      .ticks_per_second = options_.ticks_per_second,
  });

  return absl::OkStatus();
}

absl::Status
GestureController::HandleCancelGesture(const DockSwipeEvent &swipe_event) {
  DVLOG(1) << "HandleCancelGesture(): BEGIN";
  auto cleanup =
      absl::MakeCleanup([] { DVLOG(1) << "HandleCancelGesture(): END"; });

  const absl::Duration duration =
      CalculateAnimationDuration(animator_->position(), target_position_,
                                 options_.animation_duration_per_space);

  DVLOG(1) << "HandleCancelGesture():  progress=" << swipe_event.progress;
  DVLOG(1) << "HandleCancelGesture():  initial_position_=" << initial_position_;
  DVLOG(1) << "HandleCancelGesture():  current_position="
           << animator_->position();
  DVLOG(1) << "HandleCancelGesture():  target_position_=" << target_position_;
  DVLOG(1) << "HandleCancelGesture():  duration=" << duration;

  active_animation_future_ = animator_->AnimateToPosition({
      .target_position = initial_position_,
      .duration = duration,
      .easing_function = GetEasingFunction(options_.easing_function_type),
      .ticks_per_second = options_.ticks_per_second,
  });

  return absl::OkStatus();
}

} // namespace fasterswiper
