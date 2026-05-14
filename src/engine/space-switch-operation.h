#pragma once

#include "src/engine/axis-adapter.h"

namespace fasterswiper {

class SpaceSwitchOperation {
public:
  explicit SpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter);
  virtual ~SpaceSwitchOperation();

  // Non-copyable, non-movable.
  SpaceSwitchOperation(const SpaceSwitchOperation &) = delete;
  SpaceSwitchOperation &operator=(const SpaceSwitchOperation &) = delete;
  SpaceSwitchOperation(SpaceSwitchOperation &&) = delete;
  SpaceSwitchOperation &operator=(SpaceSwitchOperation &&) = delete;

  [[nodiscard]] const AxisAdapter &axis_adapter() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] int64_t position() const ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limits() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetPosition(int64_t new_position) ABSL_LOCKS_EXCLUDED(mutex_);

  void Commit() ABSL_LOCKS_EXCLUDED(mutex_);

protected:
  [[nodiscard]] virtual int64_t position_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_) = 0;
  virtual void SetPositionLocked(int64_t new_position)
      ABSL_SHARED_LOCKS_REQUIRED(mutex_) = 0;
  virtual void CommitLocked() ABSL_SHARED_LOCKS_REQUIRED(mutex_) = 0;

  [[nodiscard]] const AxisAdapter &axis_adapter_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void PostEvent(int phase, double progress,
                 std::optional<double> velocity = std::nullopt)
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

private:
  const std::unique_ptr<AxisAdapter> axis_adapter_;

  mutable absl::Mutex mutex_;
  bool is_committed_ ABSL_GUARDED_BY(mutex_) = false;
};

class ContinuousSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  ContinuousSpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter);

private:
  const int64_t origin_position_ = 0;

  bool gesture_started_ = false;
  int64_t current_position_ = 0;
  std::optional<int64_t> deferred_position_;
  int64_t latest_direction_ = 0;

  [[nodiscard]] int64_t distance_from_origin() const;
  [[nodiscard]] double progress_from_origin() const;

  [[nodiscard]] int64_t position_locked() const override;
  void SetPositionLocked(int64_t new_position) override;
  void CommitLocked() override;
};

class SegmentedSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  SegmentedSpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter);

private:
  const int64_t operation_origin_position_ = 0;

  bool gesture_started_ = false;
  int64_t origin_position_ = 0;
  int64_t current_position_ = 0;

  [[nodiscard]] int64_t position_locked() const override;
  void SetPositionLocked(int64_t new_position) override;
  void CommitLocked() override;
};

} // namespace fasterswiper
