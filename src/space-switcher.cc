#include "src/space-switcher.h"

#include "src/cf-util.h"
#include "src/const.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/mission-control.h"
#include "src/periodic-timer.h"
#include "src/space-state.h"

#include <cfloat>
#include <optional>
#include <thread>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CGEvent.h>

#include "absl/log/log.h"
#include "src/string-util.h"

namespace fasterswiper {

namespace {

constexpr double kEpsilon = FLT_TRUE_MIN;
constexpr double kInstantSwitchVelocity = 50;

} // namespace

SpaceSwitchOperation::SpaceSwitchOperation(SpaceState space_state)
    : space_state_(std::move(space_state)),
      origin_position_(space_state_.index() * kOneSwipeInNanoswipes),
      current_position_(origin_position_) {}

SpaceSwitchOperation::~SpaceSwitchOperation() {
  absl::MutexLock lock(mutex_);
  if (!is_committed_) {
    LOG(FATAL)
        << "SpaceSwitchOperation must be commited before being destroyed";
  }
}

int64_t SpaceSwitchOperation::position() const {
  absl::MutexLock lock(mutex_);
  return current_position_;
}

std::pair<int64_t, int64_t> SpaceSwitchOperation::position_soft_limit() const {
  absl::MutexLock lock(mutex_);
  return unlocked_position_soft_limit();
}

std::pair<int64_t, int64_t>
SpaceSwitchOperation::unlocked_position_soft_limit() const {
  return {0, static_cast<int64_t>(space_state_.count() - 1) *
                 kOneSwipeInNanoswipes};
}

int64_t SpaceSwitchOperation::distance_from_origin() const {
  return current_position_ - origin_position_;
}

double SpaceSwitchOperation::progress_from_origin() const {
  return space_state_.SwipesToProgress(distance_from_origin());
}

namespace {

void PostGestureEvent(int phase, double progress,
                      std::optional<double> velocity = std::nullopt) {
  auto dock = WrapCFUnique(CGEventCreate(NULL));
  if (!dock)
    return;

  CGEventSetIntegerValueField(dock.get(), kCGSEventTypeField,
                              static_cast<int64_t>(kCGSEventDockControl));
  CGEventSetIntegerValueField(dock.get(), kCGEventGestureHIDType,
                              kIOHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(dock.get(), kCGEventGesturePhase, phase);
  CGEventSetIntegerValueField(dock.get(), kCGEventGestureSwipeMotion,
                              kCGGestureMotionHorizontal);
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeProgress,
                             progress);

  if (velocity.has_value()) {
    CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeVelocityX,
                               *velocity);
  }

  VLOG(1) << "Posting event to session: " << CFEventToDebugString(dock.get());
  CGEventPost(kCGSessionEventTap, dock.get());
}

} // namespace

void SpaceSwitchOperation::SetPosition(int64_t new_position) {
  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN SetPosition(new_position=" << new_position
          << "): current_position_=" << current_position_;
  SetPositionLocked(new_position);
  VLOG(1) << "END SetPosition(" << new_position
          << "): current_position_=" << current_position_;
}

void SpaceSwitchOperation::SetPositionLocked(int64_t new_position) {
  if (is_committed_) {
    LOG(FATAL)
        << "Cannot call SetPosition() on a committed SpaceSwitchOperation";
  }

  if (new_position == current_position_) {
    return;
  }

  latest_direction_ = (new_position - current_position_) > 0 ? 1 : -1;

  if (!gesture_started_) {
    PostGestureEvent(kGestureBegan, kEpsilon * latest_direction_);
    gesture_started_ = true;
  }

  const int64_t remainder = std::abs(new_position % kOneSwipeInNanoswipes);
  const int64_t distance_to_space_threshold =
      std::min(remainder, kOneSwipeInNanoswipes - remainder);

  constexpr int64_t defer_abs_threshold = 1;
  if (distance_to_space_threshold <= defer_abs_threshold) {
    const int64_t threshold = (new_position + defer_abs_threshold) /
                              kOneSwipeInNanoswipes * kOneSwipeInNanoswipes;
    current_position_ = threshold - defer_abs_threshold * latest_direction_;
    deferred_position_ = new_position;
  } else {
    current_position_ = new_position;
    deferred_position_ = std::nullopt;
  }

  PostGestureEvent(kGestureChanged, progress_from_origin());
}

void SpaceSwitchOperation::Commit() {
  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN Commit()";
  CommitLocked();
  VLOG(1) << "END Commit()";
}

void SpaceSwitchOperation::CommitLocked() {
  if (is_committed_) {
    LOG(ERROR) << "SpaceSwitchOperation already committed";
    return;
  }

  VLOG(1) << "Commit(): space_state_=" << space_state_
          << ", origin_position_=" << origin_position_
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
    PostGestureEvent(kGestureCancelled, kEpsilon * latest_direction_ * -1,
                     kEpsilon * latest_direction_);
    is_committed_ = true;
    return;
  }

  const double direction_from_origin_to_target =
      distance_from_origin() > 0 ? 1 : -1;

  if (num_spaces == 1) {
    PostGestureEvent(kGestureEnded, progress_from_origin(),
                     kEpsilon * direction_from_origin_to_target);
  } else {
    bool is_mission_control_visible = false;
    if (auto maybe_is_visible = IsMissionControlVisible();
        maybe_is_visible.ok()) {
      is_mission_control_visible = *maybe_is_visible;
    } else {
      LOG(ERROR) << "Failed to determine if Mission Control is visible: "
                 << maybe_is_visible.status();
    }

    VLOG(1) << "Commit(): is_mission_control_visible="
            << is_mission_control_visible;

    // In the following conditions:
    //   * We're not in Mission Control
    //   * We've moved more than one space in this operation
    // The commit gesture velocities needs to match the direction of the
    // latest movement that occurred prior to the commit, not the direction of
    // movement from origin to target. I've no idea why.
    const int64_t gesture_direction = is_mission_control_visible
                                          ? direction_from_origin_to_target
                                          : latest_direction_;

    PostGestureEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
                     kInstantSwitchVelocity * latest_direction_);

    for (int i = 0; i < num_spaces - 1; i++) {
      PostGestureEvent(kGestureBegan,
                       kEpsilon * direction_from_origin_to_target);
      PostGestureEvent(kGestureChanged,
                       kEpsilon * direction_from_origin_to_target);
      PostGestureEvent(kGestureEnded,
                       kEpsilon * direction_from_origin_to_target,
                       kInstantSwitchVelocity * gesture_direction);
    }
  }

  // Wait for WindowServer to commit the space change.
  const int cid = SLSMainConnectionID();
  const CFSharedPtr<CFStringRef> display_id = space_state_.display_id();

  const int64_t original_space_id =
      space_state_.space_ids()[space_state_.index()];
  int64_t new_space_id = 0;

  const int64_t start_time = UptimeInNanoseconds();
  const int64_t deadline =
      start_time + absl::ToInt64Nanoseconds(absl::Milliseconds(200));
  while (UptimeInNanoseconds() < deadline) {
    new_space_id = SLSManagedDisplayGetCurrentSpace(cid, display_id.get());

    if (original_space_id != new_space_id) {
      const int64_t commit_latency_ns = UptimeInNanoseconds() - start_time;
      VLOG(1) << "Commit(): took " << commit_latency_ns / 1e6 << "ms";
      break;
    }

    VLOG_EVERY_N_SEC(1, 0.1)
        << "WaitForPendingCommit: waiting for gesture commit";

    std::this_thread::yield();
  }

  if (UptimeInNanoseconds() >= deadline) {
    LOG(ERROR) << "Waiting for pending commit exceeded deadline, bailing out";
  }

  is_committed_ = true;
}

} // namespace fasterswiper
