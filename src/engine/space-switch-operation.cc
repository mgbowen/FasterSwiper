#include "src/engine/space-switch-operation.h"

#include "src/cf-util.h"
#include "src/engine/const.h"
#include "src/event.h"
#include "src/gesture-serialization.h"
#include "src/macos-private.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CGEvent.h>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/status/status_macros.h>

namespace fasterswiper {

namespace {

constexpr absl::Duration kCommitDeadline = absl::Milliseconds(50);

// Signed velocity_y on kGestureEnded (> 5.0 selects WindowTransitionDisplay's
// 0.15 s critical release spring over 0.25 s if any residual motion remains).
constexpr double kCompensatedEndVelocity = 6.0;

// Hard upper bound on the post-duration hold-in-tracking settle tail.
constexpr double kMaxSettleTailSec = 0.250;

SpaceSwitchOperation::Options Resolve(SpaceSwitchOperation::Options options) {
  if (!options.natural_scrolling_enabled.has_value()) {
    options.natural_scrolling_enabled = IsNaturalScrollingEnabled();
  }

  return options;
}

// Integer division that rounds toward negative infinity, unlike C++ integer
// division which truncates toward zero. b must be positive.
int64_t FloorDiv(int64_t a, int64_t b) {
  return a / b - (a % b != 0 && ((a < 0) != (b < 0)));
}

int Sign(auto spaceship_operator_result) {
  if (spaceship_operator_result > 0) {
    return 1;
  }

  if (spaceship_operator_result < 0) {
    return -1;
  }

  return 0;
}

}  // namespace

void CGEventPostSink::Post(CGEventRef absl_nonnull event) {
  CGEventPost(kCGSessionEventTap, event);
}

void CGEventTapPostEventSink::Post(CGEventRef absl_nonnull event) {
  CGEventTapPostEvent(proxy_, event);
}

SpaceSwitchOperation::SpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> axis_adapter, Options options)
    : axis_adapter_(std::move(axis_adapter)), options_(Resolve(options)) {
  VLOG(1) << absl::StrCat("SpaceSwitchOperation(\"",
                          axis_adapter_->debug_name(), "\", ", options_, ")");
}

SpaceSwitchOperation::~SpaceSwitchOperation() {
  absl::MutexLock lock(mutex_);
  if (!is_committed_) {
    LOG(FATAL)
        << "SpaceSwitchOperation must be committed before being destroyed";
  }
}

const AxisAdapter& SpaceSwitchOperation::axis_adapter() const {
  absl::MutexLock lock(mutex_);
  return axis_adapter_locked();
}

const AxisAdapter& SpaceSwitchOperation::axis_adapter_locked() const {
  return *axis_adapter_;
}

int64_t SpaceSwitchOperation::position() const {
  absl::MutexLock lock(mutex_);
  return position_locked();
}

std::pair<int64_t, int64_t> SpaceSwitchOperation::position_soft_limits() const {
  absl::MutexLock lock(mutex_);
  return position_soft_limits_locked();
}

std::pair<int64_t, int64_t> SpaceSwitchOperation::position_soft_limits_locked()
    const {
  return axis_adapter_->position_soft_limits();
}

void SpaceSwitchOperation::SetPosition(int64_t new_position,
                                       CGEventSink* absl_nonnull event_sink) {
  CHECK(event_sink != nullptr);

  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN SetPosition(new_position=" << new_position
          << "): current_position=" << position_locked();
  SetPositionLocked(new_position, event_sink);
  VLOG(1) << "END SetPosition(" << new_position
          << "): current_position_=" << position_locked();
}

SpaceSwitchOperation::AnimationStepResult SpaceSwitchOperation::StepAnimation(
    int64_t start_position, int64_t target_position,
    int64_t time_since_start_ns, absl::Duration duration,
    const EasingFunction& easing, CGEventSink* absl_nonnull event_sink) {
  CHECK(easing != nullptr);
  CHECK(event_sink != nullptr);

  absl::MutexLock lock(mutex_);
  return StepAnimationLocked(start_position, target_position,
                             time_since_start_ns, duration, easing, event_sink);
}

SpaceSwitchOperation::AnimationStepResult
SpaceSwitchOperation::StepAnimationLocked(
    int64_t start_position, int64_t target_position,
    int64_t time_since_start_ns, absl::Duration duration,
    const EasingFunction& easing, CGEventSink* absl_nonnull event_sink) {
  const double total_ns = absl::ToDoubleNanoseconds(duration);
  const double elapsed_ns = static_cast<double>(time_since_start_ns);
  const double linear_t =
      total_ns <= 0 ? 1.0 : std::clamp(elapsed_ns / total_ns, 0.0, 1.0);
  const double eased_t = easing(linear_t);

  const int64_t interpolated_position =
      start_position + static_cast<int64_t>(std::round(
                           (target_position - start_position) * eased_t));

  const bool finished =
      linear_t >= 1.0 || interpolated_position == target_position;

  SetPositionLocked(finished ? target_position : interpolated_position,
                    event_sink);

  return finished ? AnimationStepResult::kFinished
                  : AnimationStepResult::kContinue;
}

void SpaceSwitchOperation::Commit(CGEventSink* absl_nonnull event_sink) {
  CHECK(event_sink != nullptr);

  absl::MutexLock lock(mutex_);
  VLOG(1) << "BEGIN Commit()";

  if (is_committed_) {
    VLOG(1) << "Already committed";
  } else {
    CommitLocked(event_sink);
    is_committed_ = true;
  }

  VLOG(1) << "END Commit()";
}

void SpaceSwitchOperation::PostEvent(CGEventSink* absl_nonnull event_sink,
                                     int phase, double progress,
                                     std::optional<double> velocity) const {
  CHECK(event_sink != nullptr);
  CHECK(options_.natural_scrolling_enabled.has_value());

  CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
      phase, static_cast<int>(axis_adapter_->movement_direction()), progress,
      velocity, *options_.natural_scrolling_enabled);
  VLOG(1) << "PostEvent(): event=" << CFEventToDebugString(event.get());

  absl::StatusOr<CFUniquePtr<CGEventRef>> maybe_augmented_event =
      AugmentCGEvent(event.get());
  if (maybe_augmented_event.ok()) {
    event_sink->Post(maybe_augmented_event->get());
  } else {
    LOG(ERROR) << "Failed to augment CGEvent: "
               << maybe_augmented_event.status();
  }
}

absl::StatusOr<std::unique_ptr<ContinuousSpaceSwitchOperation>>
ContinuousSpaceSwitchOperation::Create(
    std::unique_ptr<AxisAdapter> axis_adapter, Options options) {
  ASSIGN_OR_RETURN(const int64_t origin_position,
                   axis_adapter->committed_position());
  return absl::WrapUnique(new ContinuousSpaceSwitchOperation(
      std::move(axis_adapter), origin_position, options));
}

ContinuousSpaceSwitchOperation::ContinuousSpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> axis_adapter, int64_t origin_position,
    Options options)
    : SpaceSwitchOperation(std::move(axis_adapter), options),
      origin_position_(origin_position),
      current_position_(origin_position_) {}

int64_t ContinuousSpaceSwitchOperation::distance_from_origin() const {
  return *current_position_ - origin_position_;
}

double ContinuousSpaceSwitchOperation::progress_from_origin() const {
  return axis_adapter_locked().NanoswipesToProgress(distance_from_origin());
}

int64_t ContinuousSpaceSwitchOperation::position_locked() const {
  return *current_position_;
}

void ContinuousSpaceSwitchOperation::SetPositionLocked(
    int64_t new_position, CGEventSink* absl_nonnull event_sink) {
  if (new_position == current_position_) {
    return;
  }

  latest_direction_ = (new_position - *current_position_) > 0 ? 1 : -1;

  if (!gesture_started_) {
    PostEvent(event_sink, kGestureBegan, kEpsilon * latest_direction_);
    gesture_started_ = true;
  }

  current_position_.Set(new_position);
  PostEvent(event_sink, kGestureChanged, progress_from_origin());
}

void ContinuousSpaceSwitchOperation::CommitLocked(
    CGEventSink* absl_nonnull event_sink) {
  VLOG(1) << "Commit(): origin_position_=" << origin_position_
          << ", current_position_=" << current_position_
          << ", latest_direction_=" << latest_direction_
          << ", distance_from_origin=" << distance_from_origin()
          << ", progress_from_origin=" << progress_from_origin();

  current_position_.CommitDeferred();

  const int64_t num_spaces =
      std::abs(distance_from_origin() / kOneSwipeInNanoswipes);

  if (distance_from_origin() == 0) {
    PostEvent(event_sink, kGestureCancelled, kEpsilon * latest_direction_ * -1,
              kEpsilon * latest_direction_);
  } else {
    const double direction_from_origin_to_target =
        distance_from_origin() > 0 ? 1 : -1;

    PostEvent(event_sink, kGestureEnded,
              kEpsilon * direction_from_origin_to_target,
              kInstantSwitchVelocity * latest_direction_);

    for (int i = 0; i < num_spaces - 1; i++) {
      PostEvent(event_sink, kGestureBegan,
                kEpsilon * direction_from_origin_to_target);
      PostEvent(event_sink, kGestureEnded,
                kEpsilon * direction_from_origin_to_target,
                kInstantSwitchVelocity * latest_direction_);
    }

    (void)axis_adapter_locked().WaitForCommittedPositionChanged(
        origin_position_, kCommitDeadline);
  }
}

absl::StatusOr<std::unique_ptr<SegmentedSpaceSwitchOperation>>
SegmentedSpaceSwitchOperation::Create(std::unique_ptr<AxisAdapter> axis_adapter,
                                      Options options) {
  ASSIGN_OR_RETURN(const int64_t operation_origin_position,
                   axis_adapter->committed_position());
  return absl::WrapUnique(new SegmentedSpaceSwitchOperation(
      std::move(axis_adapter), operation_origin_position, options));
}

SegmentedSpaceSwitchOperation::SegmentedSpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> axis_adapter,
    int64_t operation_origin_position, Options options)
    : SpaceSwitchOperation(std::move(axis_adapter), options),
      operation_origin_position_(operation_origin_position),
      current_position_(operation_origin_position_) {
  VLOG(1) << "SegmentedSpaceSwitchOperation(): operation_origin_position_="
          << operation_origin_position_;
}

std::string SegmentedSpaceSwitchOperation::StateToString(const State& state) {
  return std::visit([](const auto& state) { return absl::StrCat(state); },
                    state);
}

int64_t SegmentedSpaceSwitchOperation::position_locked() const {
  return *current_position_;
}

void SegmentedSpaceSwitchOperation::SetPositionLocked(
    int64_t new_position, CGEventSink* absl_nonnull event_sink) {
  if (new_position == *current_position_) {
    return;
  }

  // Progress toward the target position, starting and ending gestures as
  // needed.
  while (current_position_.deferred() != new_position) {
    const bool is_moving_positive = new_position > current_position_;

    if (std::holds_alternative<States::Idle>(state_)) {
      SetState(States::GestureActive{
          .origin_position = current_position_.deferred(),
      });

      PostEvent(event_sink, kGestureBegan,
                is_moving_positive ? kEpsilon : -kEpsilon);
    }

    auto& gesture_active = std::get<States::GestureActive>(state_);

    const int64_t next_boundary = GetNextBoundary(is_moving_positive);
    VLOG(1) << "next_boundary=" << next_boundary;

    current_position_.Set(
        ((is_moving_positive && new_position >= next_boundary) ||
         (!is_moving_positive && new_position <= next_boundary))
            ? next_boundary
            : new_position);
    VLOG(1) << "current_position_=" << current_position_;

    const bool is_boundary_reached =
        current_position_.deferred() == next_boundary;
    VLOG(1) << "is_boundary_reached=" << is_boundary_reached;

    if (is_boundary_reached) {
      EndGesture(gesture_active, event_sink);
    } else {
      const double progress = axis_adapter_locked().NanoswipesToProgress(
          *current_position_ - gesture_active.origin_position);
      PostEvent(event_sink, kGestureChanged, progress);
    }
  }
}

void SegmentedSpaceSwitchOperation::SetState(State new_state) {
  VLOG(1) << "SegmentedSpaceSwitchOperation::SetState(): new_state="
          << StateToString(new_state);
  state_ = new_state;
}

void SegmentedSpaceSwitchOperation::EndGesture(
    const States::GestureActive& gesture_active,
    CGEventSink* absl_nonnull event_sink) {
  if (!current_position_.has_deferred()) {
    LOG(ERROR) << "current_position_ is not deferred!";
  }

  // We've reached a boundary between spaces, commit the pending space
  // transition.
  //
  // How we commit the transition depends on the gesture origin position
  // and the target position we're moving towards. If we're moving away
  // from the origin, we end the gesture, but if we're moving back towards
  // the origin, we cancel it; in macOS' parlance, ending a gesture
  // indicates the user wants to move to an adjacent space, whereas
  // cancelling a gesture indicates they want to return to the space they
  // were at when they started the gesture.
  const int64_t origin_to_effective_position_sign =
      Sign(*current_position_ <=> gesture_active.origin_position);
  const int64_t current_to_new_position_sign =
      Sign(current_position_.deferred() <=> *current_position_);

  const auto [soft_min, soft_max] = position_soft_limits_locked();
  const bool is_rubberbanding =
      (current_position_ < soft_min && current_to_new_position_sign > 0) ||
      (current_position_ > soft_max && current_to_new_position_sign < 0);

  VLOG(1) << "Boundary reached, origin_to_effective_position_sign="
          << origin_to_effective_position_sign
          << ", current_to_new_position_sign=" << current_to_new_position_sign
          << ", is_rubberbanding=" << is_rubberbanding;

  if (gesture_active.origin_position == current_position_.deferred()) {
    // Ending on the gesture origin.
    const int64_t effective_sign = is_rubberbanding
                                       ? -current_to_new_position_sign
                                       : current_to_new_position_sign;
    const double signed_epsilon = kEpsilon * effective_sign;
    PostEvent(event_sink, kGestureChanged, signed_epsilon);
    PostEvent(event_sink, kGestureCancelled, signed_epsilon, signed_epsilon);
  } else {
    // Moving away from the gesture origin.
    const double progress = axis_adapter_locked().NanoswipesToProgress(
        current_position_.deferred() - gesture_active.origin_position);
    PostEvent(event_sink, kGestureChanged, progress);
    PostEvent(event_sink, kGestureEnded, progress,
              kEpsilon * current_to_new_position_sign);
  }

  current_position_.CommitDeferred();
  SetState(States::Idle{});
}

void SegmentedSpaceSwitchOperation::CommitLocked(
    CGEventSink* absl_nonnull event_sink) {
  if (const auto* gesture_active =
          std::get_if<States::GestureActive>(&state_)) {
    if (current_position_.deferred() % kOneSwipeInNanoswipes != 0) {
      current_position_.Set(
          FloorDiv(current_position_.deferred(), kOneSwipeInNanoswipes) *
          kOneSwipeInNanoswipes);
    }

    EndGesture(*gesture_active, event_sink);
  }

  if (operation_origin_position_ != current_position_.deferred()) {
    (void)axis_adapter_locked().WaitForCommittedPositionChanged(
        operation_origin_position_, kCommitDeadline);
  }
}

int64_t SegmentedSpaceSwitchOperation::GetNextBoundary(
    bool is_moving_positive) {
  VLOG(2) << "GetNextBoundary(): is_moving_positive=" << is_moving_positive
          << ", current_position_=" << current_position_
          << ", operation_origin_position_=" << operation_origin_position_;

  const auto& gesture_active = std::get<States::GestureActive>(state_);

  const int64_t positive_boundary =
      (FloorDiv(gesture_active.origin_position + 1, kOneSwipeInNanoswipes) +
       1) *
      kOneSwipeInNanoswipes;
  VLOG(2) << "GetNextBoundary(): positive_boundary=" << positive_boundary;

  const int64_t negative_boundary =
      FloorDiv(gesture_active.origin_position - 1, kOneSwipeInNanoswipes) *
      kOneSwipeInNanoswipes;
  VLOG(2) << "GetNextBoundary(): negative_boundary=" << negative_boundary;

  const int64_t initial_next_boundary =
      is_moving_positive ? positive_boundary : negative_boundary;
  VLOG(2) << "GetNextBoundary(): initial_next_boundary="
          << initial_next_boundary;

  const auto [soft_min, soft_max] = position_soft_limits_locked();

  if (initial_next_boundary < soft_min) {
    return is_moving_positive ? soft_min : std::numeric_limits<int64_t>::min();
  }

  if (initial_next_boundary > soft_max) {
    return is_moving_positive ? std::numeric_limits<int64_t>::max() : soft_max;
  }

  return initial_next_boundary;
}

absl::StatusOr<std::unique_ptr<CompensatedVerticalSpaceSwitchOperation>>
CompensatedVerticalSpaceSwitchOperation::Create(
    std::unique_ptr<AxisAdapter> axis_adapter, Options options, ClockFn clock) {
  CHECK(clock != nullptr);
  ASSIGN_OR_RETURN(const int64_t operation_origin_position,
                   axis_adapter->committed_position());
  return absl::WrapUnique(new CompensatedVerticalSpaceSwitchOperation(
      std::move(axis_adapter), operation_origin_position, options,
      std::move(clock)));
}

CompensatedVerticalSpaceSwitchOperation::
    CompensatedVerticalSpaceSwitchOperation(
        std::unique_ptr<AxisAdapter> axis_adapter,
        int64_t operation_origin_position, Options options, ClockFn clock)
    : SpaceSwitchOperation(std::move(axis_adapter), options),
      operation_origin_position_(operation_origin_position),
      clock_(std::move(clock)),
      logical_position_(operation_origin_position) {
  CHECK(clock_ != nullptr);
  compensator_.Reset(0.0, 0.0, clock_());
}

int64_t CompensatedVerticalSpaceSwitchOperation::position_locked() const {
  if (const auto* active = std::get_if<States::GestureActive>(&state_)) {
    if (active->is_rubberbanding) {
      return logical_position_;
    }
    const SpringState estimated = compensator_.EstimateStateAt(clock_());
    const double clamped_y = std::clamp(estimated.y, 0.0, 1.0);
    return active->origin_position +
           active->direction * static_cast<int64_t>(std::llround(
                                   clamped_y * kOneSwipeInNanoswipes));
  }

  return logical_position_;
}

void CompensatedVerticalSpaceSwitchOperation::StartSegmentLocked(
    int64_t dir, int64_t now_ns, CGEventSink* absl_nonnull event_sink) {
  const int64_t end_boundary = logical_position_ + dir * kOneSwipeInNanoswipes;
  const auto [soft_min, soft_max] = position_soft_limits_locked();
  const bool is_rubberbanding =
      end_boundary < soft_min || end_boundary > soft_max;
  state_ = States::GestureActive{
      .origin_position = logical_position_,
      .direction = dir,
      .is_rubberbanding = is_rubberbanding,
  };
  PostEvent(event_sink, kGestureBegan, dir * kEpsilon);
  compensator_.Reset(0.0, 0.0, now_ns);
}

void CompensatedVerticalSpaceSwitchOperation::SetPositionLocked(
    int64_t new_position, CGEventSink* absl_nonnull event_sink) {
  intended_target_position_.reset();
  const int64_t now_ns = clock_();

  if (new_position == logical_position_) {
    if (const auto* active = std::get_if<States::GestureActive>(&state_)) {
      if (!active->is_rubberbanding) {
        const double r = std::clamp(
            static_cast<double>((new_position - active->origin_position) *
                                active->direction) /
                kOneSwipeInNanoswipes,
            0.0, 1.0);
        const double u = compensator_.StepPhysicalDrag(r, now_ns);
        PostEvent(event_sink, kGestureChanged, active->direction * u);
      }
    }
    return;
  }

  const auto [soft_min, soft_max] = position_soft_limits_locked();

  while (logical_position_ != new_position) {
    if (std::holds_alternative<States::Idle>(state_)) {
      StartSegmentLocked(new_position > logical_position_ ? 1 : -1, now_ns,
                         event_sink);
    }

    const auto active = std::get<States::GestureActive>(state_);
    const int64_t origin = active.origin_position;
    const int64_t dir = active.direction;

    if ((new_position - origin) * dir <= 0) {
      EndSegmentLocked(origin, now_ns, event_sink);
      continue;
    }

    if (active.is_rubberbanding) {
      const double raw_r = static_cast<double>((new_position - origin) * dir) /
                           kOneSwipeInNanoswipes;
      PostEvent(event_sink, kGestureChanged, dir * std::max(kEpsilon, raw_r));
      logical_position_ = new_position;
      break;
    }

    const int64_t end_boundary = origin + dir * kOneSwipeInNanoswipes;
    const int64_t next_end_boundary =
        end_boundary + dir * kOneSwipeInNanoswipes;
    const bool has_further_segment =
        next_end_boundary >= soft_min && next_end_boundary <= soft_max;

    if (has_further_segment && (new_position - end_boundary) * dir > 0) {
      EndSegmentLocked(end_boundary, now_ns, event_sink);
      continue;
    }

    // Hold in GestureActive at end_boundary when no further in-limits segment
    // exists (or at exact boundary) so finger lift still gets the settle tail.
    const bool at_or_past_end = (new_position - end_boundary) * dir >= 0;
    const int64_t clamped_position =
        at_or_past_end ? end_boundary : new_position;
    const double r =
        std::clamp(static_cast<double>((clamped_position - origin) * dir) /
                       kOneSwipeInNanoswipes,
                   0.0, 1.0);
    const double u = compensator_.StepPhysicalDrag(r, now_ns);
    PostEvent(event_sink, kGestureChanged, dir * u);
    logical_position_ = clamped_position;
    break;
  }
}

SpaceSwitchOperation::AnimationStepResult
CompensatedVerticalSpaceSwitchOperation::StepAnimationLocked(
    int64_t start_position, int64_t target_position,
    int64_t time_since_start_ns, absl::Duration duration,
    const EasingFunction& easing, CGEventSink* absl_nonnull event_sink) {
  intended_target_position_ = target_position;
  const int64_t now_ns = clock_();

  if (std::holds_alternative<States::Idle>(state_)) {
    if (target_position == logical_position_) {
      return AnimationStepResult::kFinished;
    }
    StartSegmentLocked(target_position > logical_position_ ? 1 : -1, now_ns,
                       event_sink);
  }

  const double duration_s = absl::ToDoubleSeconds(duration);
  const double elapsed_s = static_cast<double>(time_since_start_ns) * 1e-9;
  const double linear_s =
      duration_s <= 0.0 ? 1.0 : std::clamp(elapsed_s / duration_s, 0.0, 1.0);
  const double raw_eased_s = easing(linear_s);

  if (const auto* active = std::get_if<States::GestureActive>(&state_);
      active != nullptr && active->is_rubberbanding) {
    const int64_t origin = active->origin_position;
    const int64_t dir = active->direction;
    const int64_t ref_pos =
        start_position + static_cast<int64_t>(std::round(
                             (target_position - start_position) * raw_eased_s));

    if ((target_position - origin) * dir < 0 && (ref_pos - origin) * dir <= 0) {
      EndSegmentLocked(origin, now_ns, event_sink);
      StartSegmentLocked(target_position > logical_position_ ? 1 : -1, now_ns,
                         event_sink);
    } else {
      const bool finished = linear_s >= 1.0 || ref_pos == target_position;
      const int64_t next_pos = finished ? target_position : ref_pos;
      if ((next_pos - origin) * dir > 0) {
        const double raw_r = static_cast<double>((next_pos - origin) * dir) /
                             kOneSwipeInNanoswipes;
        PostEvent(event_sink, kGestureChanged, dir * std::max(kEpsilon, raw_r));
      }
      logical_position_ = next_pos;
      return finished ? AnimationStepResult::kFinished
                      : AnimationStepResult::kContinue;
    }
  }

  const double clamped_eased_s = std::clamp(raw_eased_s, 0.0, 1.0);
  const int64_t ref_pos =
      start_position +
      static_cast<int64_t>(
          std::llround((target_position - start_position) * clamped_eased_s));

  // Split across intermediate segment boundaries (multi-segment forward or
  // cross-origin reversal).
  while (const auto* cur = std::get_if<States::GestureActive>(&state_)) {
    if (cur->is_rubberbanding) {
      break;
    }
    const int64_t origin = cur->origin_position;
    const int64_t dir = cur->direction;
    const int64_t end_boundary = origin + dir * kOneSwipeInNanoswipes;

    if ((target_position - end_boundary) * dir > 0 &&
        (ref_pos - end_boundary) * dir >= 0) {
      EndSegmentLocked(end_boundary, now_ns, event_sink);
      StartSegmentLocked(target_position > logical_position_ ? 1 : -1, now_ns,
                         event_sink);
    } else if ((target_position - origin) * dir < 0 &&
               (ref_pos - origin) * dir <= 0) {
      EndSegmentLocked(origin, now_ns, event_sink);
      StartSegmentLocked(target_position > logical_position_ ? 1 : -1, now_ns,
                         event_sink);
    } else {
      break;
    }
  }

  const auto current_active = std::get<States::GestureActive>(state_);
  const int64_t active_origin = current_active.origin_position;
  const int64_t active_dir = current_active.direction;

  if (current_active.is_rubberbanding) {
    const bool finished = linear_s >= 1.0 || ref_pos == target_position;
    const int64_t next_pos = finished ? target_position : ref_pos;
    if ((next_pos - active_origin) * active_dir > 0) {
      const double raw_r =
          static_cast<double>((next_pos - active_origin) * active_dir) /
          kOneSwipeInNanoswipes;
      PostEvent(event_sink, kGestureChanged,
                active_dir * std::max(kEpsilon, raw_r));
    }
    logical_position_ = next_pos;
    return finished ? AnimationStepResult::kFinished
                    : AnimationStepResult::kContinue;
  }

  const double start_r =
      static_cast<double>((start_position - active_origin) * active_dir) /
      kOneSwipeInNanoswipes;
  const double target_r =
      static_cast<double>((target_position - active_origin) * active_dir) /
      kOneSwipeInNanoswipes;
  const VerticalSpringCompensator::AnimationTickResult tick =
      compensator_.StepAnimationTick(elapsed_s, duration_s, start_r, target_r,
                                     easing, now_ns);
  logical_position_ = ref_pos;

  if (duration_s <= 0.0) {
    return AnimationStepResult::kFinished;
  }

  PostEvent(event_sink, kGestureChanged, active_dir * tick.command);
  if (elapsed_s >= duration_s + kMaxSettleTailSec || tick.settled) {
    return AnimationStepResult::kFinished;
  }
  return AnimationStepResult::kContinue;
}

void CompensatedVerticalSpaceSwitchOperation::EndSegmentLocked(
    int64_t final_segment_position, int64_t now_ns,
    CGEventSink* absl_nonnull event_sink) {
  const auto active = std::get<States::GestureActive>(state_);
  const int64_t origin = active.origin_position;
  const int64_t dir = active.direction;

  if (final_segment_position == origin) {
    // Segment-sign convention: emit +dir * kEpsilon matching .began and the
    // preceding 25 ms .changed dwell stream.
    PostEvent(event_sink, kGestureChanged, dir * kEpsilon);
    PostEvent(event_sink, kGestureCancelled, dir * kEpsilon, dir * kEpsilon);
  } else {
    PostEvent(event_sink, kGestureChanged, dir * 1.0);
    PostEvent(event_sink, kGestureEnded, dir * 1.0,
              dir * kCompensatedEndVelocity);
  }

  logical_position_ = final_segment_position;
  state_ = States::Idle{};
  compensator_.Reset(0.0, 0.0, now_ns);
}

void CompensatedVerticalSpaceSwitchOperation::CommitLocked(
    CGEventSink* absl_nonnull event_sink) {
  const int64_t now_ns = clock_();
  if (const auto* active = std::get_if<States::GestureActive>(&state_)) {
    const int64_t origin = active->origin_position;
    const int64_t dir = active->direction;
    const int64_t end_boundary = origin + dir * kOneSwipeInNanoswipes;

    int64_t final_pos = origin;
    if (!active->is_rubberbanding) {
      if (intended_target_position_.has_value()) {
        final_pos = (*intended_target_position_ - origin) * dir > 0
                        ? end_boundary
                        : origin;
      } else if ((logical_position_ - origin) * dir >=
                 kOneSwipeInNanoswipes / 2) {
        final_pos = end_boundary;
      }
    }

    EndSegmentLocked(final_pos, now_ns, event_sink);
  }

  intended_target_position_.reset();

  if (operation_origin_position_ != logical_position_) {
    (void)axis_adapter_locked().WaitForCommittedPositionChanged(
        operation_origin_position_, kCommitDeadline);
  }
}

}  // namespace fasterswiper
