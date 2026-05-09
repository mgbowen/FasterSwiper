#include "src/space-switcher.h"

#include "src/cf-util.h"
#include "src/const.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"
#include "src/space-state.h"

#include <cfloat>
#include <limits>
#include <optional>
#include <thread>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CGEvent.h>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace fasterswiper {

namespace {

constexpr double kEpsilon = FLT_TRUE_MIN;
constexpr double kInstantSwitchVelocity = 2000;

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

double SpaceSwitchOperation::distance_from_origin() const {
  return current_position_ - origin_position_;
}

double SpaceSwitchOperation::progress_from_origin() const {
  return space_state_.SwipesToProgress(distance_from_origin());
}

namespace {

// Integer division that rounds toward negative infinity, unlike C++ integer
// division which truncates toward zero. b must be positive.
int64_t FloorDiv(int64_t a, int64_t b) {
  return a / b - (a % b != 0 && (a ^ b) < 0);
}

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

namespace {

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

  latest_direction_ = Sign(new_position <=> current_position_);
  current_position_ = new_position;

  if (!gesture_started_) {
    PostGestureEvent(kGestureBegan, kEpsilon * latest_direction_);
    gesture_started_ = true;
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
          << ", distance_from_origin=" << distance_from_origin()
          << ", progress_from_origin=" << progress_from_origin();

  const auto [soft_min, soft_max] = unlocked_position_soft_limit();

  int64_t num_spaces = std::abs(distance_from_origin() / kOneSwipeInNanoswipes);

  if (distance_from_origin() == 0) {
    PostGestureEvent(kGestureCancelled, 0);
    is_committed_ = true;
    return;
  }

  const double direction_from_origin_to_target =
      distance_from_origin() > 0 ? 1 : -1;

  PostGestureEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
                   kInstantSwitchVelocity * direction_from_origin_to_target);

  for (int64_t i = 1; i < num_spaces; i++) {
    PostGestureEvent(kGestureBegan, kEpsilon * direction_from_origin_to_target);
    PostGestureEvent(kGestureChanged,
                     kEpsilon * direction_from_origin_to_target);
    PostGestureEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
                     kInstantSwitchVelocity * direction_from_origin_to_target);
  }

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
