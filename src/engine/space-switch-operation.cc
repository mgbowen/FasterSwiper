#include "src/engine/space-switch-operation.h"

#include "src/cf-util.h"
#include "src/engine/const.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/string-util.h"

#include <cfloat>
#include <optional>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CGEvent.h>

#include <absl/log/log.h>

namespace fasterswiper {

namespace {

// Integer division that rounds toward negative infinity, unlike C++ integer
// division which truncates toward zero. b must be positive.
int64_t FloorDiv(int64_t a, int64_t b) {
  return a / b - (a % b != 0 && (a ^ b) < 0);
}

size_t Sign(auto spaceship_operator_result) {
  if (spaceship_operator_result > 0) {
    return 1;
  }

  if (spaceship_operator_result < 0) {
    return -1;
  }

  return 0;
}

} // namespace

SpaceSwitchOperation::SpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> axis_adapter)
    : axis_adapter_(std::move(axis_adapter)) {}

SpaceSwitchOperation::~SpaceSwitchOperation() {
  absl::MutexLock lock(mutex_);
  if (!is_committed_) {
    LOG(FATAL)
        << "SpaceSwitchOperation must be committed before being destroyed";
  }
}

const AxisAdapter &SpaceSwitchOperation::axis_adapter() const {
  absl::MutexLock lock(mutex_);
  return axis_adapter_locked();
}

const AxisAdapter &SpaceSwitchOperation::axis_adapter_locked() const {
  return *axis_adapter_;
}

int64_t SpaceSwitchOperation::position() const {
  absl::MutexLock lock(mutex_);
  return position_locked();
}

std::pair<int64_t, int64_t> SpaceSwitchOperation::position_soft_limits() const {
  absl::MutexLock lock(mutex_);
  return axis_adapter_->position_soft_limits();
}

void SpaceSwitchOperation::SetPosition(int64_t new_position) {
  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN SetPosition(new_position=" << new_position
          << "): current_position=" << position_locked();
  SetPositionLocked(new_position);
  VLOG(1) << "END SetPosition(" << new_position
          << "): current_position_=" << position_locked();
}

void SpaceSwitchOperation::Commit() {
  absl::MutexLock lock(mutex_);
  VLOG(1) << "BEGIN Commit()";

  if (is_committed_) {
    VLOG(1) << "Already committed";
  } else {
    CommitLocked();
    is_committed_ = true;
  }

  VLOG(1) << "END Commit()";
}

void SpaceSwitchOperation::PostEvent(int phase, double progress,
                                     std::optional<double> velocity) {
  CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
      phase, static_cast<int>(axis_adapter_->movement_direction()), progress,
      velocity);
  VLOG(1) << "PostEvent(): event=" << CFEventToDebugString(event.get());

  CGEventPost(kCGSessionEventTap, event.get());
}

ContinuousSpaceSwitchOperation::ContinuousSpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> _axis_adapter)
    : SpaceSwitchOperation(std::move(_axis_adapter)),
      origin_position_(*axis_adapter_locked().committed_position()),
      current_position_(origin_position_) {}

int64_t ContinuousSpaceSwitchOperation::distance_from_origin() const {
  return current_position_ - origin_position_;
}

double ContinuousSpaceSwitchOperation::progress_from_origin() const {
  return axis_adapter_locked().NanoswipesToProgress(distance_from_origin());
}

int64_t ContinuousSpaceSwitchOperation::position_locked() const {
  return current_position_;
}

void ContinuousSpaceSwitchOperation::SetPositionLocked(int64_t new_position) {
  if (new_position == current_position_) {
    return;
  }

  latest_direction_ = (new_position - current_position_) > 0 ? 1 : -1;

  if (!gesture_started_) {
    PostEvent(kGestureBegan, kEpsilon * latest_direction_);
    gesture_started_ = true;
  }

  const int64_t remainder = std::abs(new_position % kOneSwipeInNanoswipes);
  const int64_t distance_to_space_threshold =
      std::min(remainder, kOneSwipeInNanoswipes - remainder);

  constexpr int64_t defer_abs_threshold = 1;
  if (distance_to_space_threshold <= defer_abs_threshold) {
    const int64_t threshold =
        (new_position >= 0 ? new_position + defer_abs_threshold
                           : new_position - defer_abs_threshold) /
        kOneSwipeInNanoswipes * kOneSwipeInNanoswipes;
    current_position_ = threshold - defer_abs_threshold * latest_direction_;
    deferred_position_ = new_position;
  } else {
    current_position_ = new_position;
    deferred_position_ = std::nullopt;
  }

  PostEvent(kGestureChanged, progress_from_origin());
}

void ContinuousSpaceSwitchOperation::CommitLocked() {
  VLOG(1) << "Commit(): origin_position_=" << origin_position_
          << ", current_position_=" << current_position_
          << ", deferred_position_=" << OptionalToString(deferred_position_)
          << ", latest_direction_=" << latest_direction_
          << ", distance_from_origin=" << distance_from_origin()
          << ", progress_from_origin=" << progress_from_origin();

  if (deferred_position_.has_value()) {
    current_position_ = *deferred_position_;
    deferred_position_ = std::nullopt;
  }

  const int64_t num_spaces =
      std::abs(distance_from_origin() / kOneSwipeInNanoswipes);

  if (distance_from_origin() == 0) {
    PostEvent(kGestureCancelled, kEpsilon * latest_direction_ * -1,
              kEpsilon * latest_direction_);
  } else {
    const double direction_from_origin_to_target =
        distance_from_origin() > 0 ? 1 : -1;

    PostEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
              kInstantSwitchVelocity * latest_direction_);

    for (int i = 0; i < num_spaces - 1; i++) {
      PostEvent(kGestureBegan, kEpsilon * direction_from_origin_to_target);
      // PostEvent(kGestureChanged, kEpsilon * direction_from_origin_to_target);
      PostEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
                kInstantSwitchVelocity * latest_direction_);
    }

    (void)axis_adapter_locked().WaitForCommittedPositionChanged(
        origin_position_, absl::Milliseconds(200));
  }
}

SegmentedSpaceSwitchOperation::SegmentedSpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> _axis_adapter)
    : SpaceSwitchOperation(std::move(_axis_adapter)),
      origin_position_(*axis_adapter_locked().committed_position()),
      current_position_(origin_position_) {}

int64_t SegmentedSpaceSwitchOperation::position_locked() const {
  return current_position_;
}

void SegmentedSpaceSwitchOperation::SetPositionLocked(int64_t new_position) {
  if (new_position == current_position_) {
    return;
  }

  const auto [soft_min, soft_max] =
      axis_adapter_locked().position_soft_limits();

  // Progress toward the target position, starting and committing gestures as
  // needed.
  while (current_position_ != new_position) {
    const bool is_moving_right = new_position > current_position_;

    if (!gesture_started_) {
      PostEvent(kGestureBegan, is_moving_right ? kEpsilon : -kEpsilon);
      gesture_started_ = true;
      origin_position_ = current_position_;
    }

    int64_t next_boundary =
        is_moving_right
            ? (FloorDiv(current_position_, kOneSwipeInNanoswipes) + 1) *
                  kOneSwipeInNanoswipes
            : FloorDiv(current_position_ - 1, kOneSwipeInNanoswipes) *
                  kOneSwipeInNanoswipes;
    if (next_boundary < soft_min) {
      next_boundary =
          is_moving_right ? soft_min : std::numeric_limits<int64_t>::min();
    } else if (next_boundary > soft_max) {
      next_boundary =
          is_moving_right ? std::numeric_limits<int64_t>::max() : soft_max;
    }

    const int64_t target_position =
        ((is_moving_right && new_position >= next_boundary) ||
         (!is_moving_right && new_position <= next_boundary))
            ? next_boundary
            : new_position;
    const int64_t distance_from_origin = target_position - origin_position_;
    const double progress_from_origin =
        axis_adapter_locked().NanoswipesToProgress(distance_from_origin);

    const bool boundary_reached =
        (target_position % kOneSwipeInNanoswipes) == 0 &&
        target_position >= soft_min && target_position <= soft_max;

    VLOG(1) << "Preparing to send gesture events, target_position="
            << target_position
            << ", progress_from_origin=" << progress_from_origin;

    if (boundary_reached) {
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
      const int64_t origin_to_current_position_sign =
          Sign(current_position_ <=> origin_position_);
      const int64_t current_to_new_position_sign =
          Sign(new_position <=> current_position_);

      const bool is_rubberbanding =
          (current_position_ < soft_min && current_to_new_position_sign > 0) ||
          (current_position_ > soft_max && current_to_new_position_sign < 0);

      VLOG(1) << "Boundary reached, origin_to_current_position_sign="
              << origin_to_current_position_sign
              << ", current_to_new_position_sign="
              << current_to_new_position_sign
              << ", is_rubberbanding=" << is_rubberbanding;

      if (origin_to_current_position_sign == 0) {
        // Instant switch to an adjacent space.
        PostEvent(kGestureChanged, kEpsilon * current_to_new_position_sign);
        PostEvent(kGestureEnded, kEpsilon * current_to_new_position_sign,
                  kInstantSwitchVelocity * current_to_new_position_sign);
      } else if (origin_to_current_position_sign ==
                 current_to_new_position_sign) {
        // Moving away from the gesture origin.
        PostEvent(kGestureChanged, progress_from_origin);
        PostEvent(kGestureEnded, progress_from_origin,
                  kEpsilon * current_to_new_position_sign);
      } else {
        // Moving towards the gesture origin.
        const double transitory_progress =
            progress_from_origin - (kEpsilon * current_to_new_position_sign);
        PostEvent(kGestureChanged, transitory_progress);

        // Velocity is based on the direction a hand would be moving to cause
        // the space movement we're making, but if we're rubberbanding, we
        // reverse the initially calculated velocity because the actual space
        // movement will be the opposite of what the "hand" is doing, e.g. if
        // we're at the soft min and the hand is moving left, the space movement
        // will initially be left, but when the hand lets go, the space will
        // move right to go back to the soft min.
        const double velocity = (is_rubberbanding ? -kEpsilon : kEpsilon) *
                                current_to_new_position_sign;
        PostEvent(kGestureCancelled, transitory_progress, velocity);
      }

      gesture_started_ = false;
    } else {
      PostEvent(kGestureChanged, progress_from_origin);
    }

    current_position_ = target_position;
  }
}

void SegmentedSpaceSwitchOperation::CommitLocked() {
  if (operation_origin_position_ - current_position_ != 0) {
    (void)axis_adapter_locked().WaitForCommittedPositionChanged(
        operation_origin_position_, absl::Milliseconds(200));
  }
}

} // namespace fasterswiper
