#pragma once

#include "src/engine/movement-engine.h"
#include "src/engine/position-reporter.h"
#include "src/space-state.h"

namespace fasterswiper {

class SpaceSwitchOperation {
public:
  explicit SpaceSwitchOperation(
      std::unique_ptr<AxisAdapter> axis_adapter,
      std::unique_ptr<MovementEngine> movement_engine);
  ~SpaceSwitchOperation();

  // Non-copyable, non-movable.
  SpaceSwitchOperation(const SpaceSwitchOperation &) = delete;
  SpaceSwitchOperation &operator=(const SpaceSwitchOperation &) = delete;
  SpaceSwitchOperation(SpaceSwitchOperation &&) = delete;
  SpaceSwitchOperation &operator=(SpaceSwitchOperation &&) = delete;

  const AxisAdapter *absl_nonnull axis_adapter() const { return axis_adapter_.get(); }

  const MovementEngine *absl_nonnull movement_engine() const {
    return movement_engine_.get();
  }

  [[nodiscard]] int64_t position() const ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limits() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetPosition(int64_t new_position) ABSL_LOCKS_EXCLUDED(mutex_);

  void Commit() ABSL_LOCKS_EXCLUDED(mutex_);

private:
  const std::unique_ptr<AxisAdapter> axis_adapter_;
  const std::unique_ptr<MovementEngine> movement_engine_;

  mutable absl::Mutex mutex_;
  bool is_committed_ ABSL_GUARDED_BY(mutex_) = false;
};

} // namespace fasterswiper
