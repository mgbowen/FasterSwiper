#pragma once

#include "src/space-state.h"
#include "src/string-util.h"

#include <variant>

namespace fasterswiper {

class SpaceSwitchOperation {
public:
  explicit SpaceSwitchOperation(SpaceState space_state);
  ~SpaceSwitchOperation();

  // Non-copyable, non-movable.
  SpaceSwitchOperation(const SpaceSwitchOperation &) = delete;
  SpaceSwitchOperation &operator=(const SpaceSwitchOperation &) = delete;
  SpaceSwitchOperation(SpaceSwitchOperation &&) = delete;
  SpaceSwitchOperation &operator=(SpaceSwitchOperation &&) = delete;

  [[nodiscard]] const SpaceState &space_state() const { return space_state_; }

  [[nodiscard]] int64_t position() const ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limit() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetPosition(int64_t new_position) ABSL_LOCKS_EXCLUDED(mutex_);

  void Commit() ABSL_LOCKS_EXCLUDED(mutex_);

private:
  const SpaceState space_state_;
  const int64_t origin_position_;

  mutable absl::Mutex mutex_;
  bool gesture_started_ ABSL_GUARDED_BY(mutex_) = false;
  int64_t current_position_ ABSL_GUARDED_BY(mutex_) = 0;
  std::optional<int64_t> deferred_position_ ABSL_GUARDED_BY(mutex_);
  int64_t latest_direction_ ABSL_GUARDED_BY(mutex_) = 0;
  bool is_committed_ ABSL_GUARDED_BY(mutex_) = false;

  [[nodiscard]] std::pair<int64_t, int64_t> unlocked_position_soft_limit() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  [[nodiscard]] int64_t distance_from_origin() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  [[nodiscard]] double progress_from_origin() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void SetPositionLocked(int64_t new_position)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void CommitLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
};

} // namespace fasterswiper
