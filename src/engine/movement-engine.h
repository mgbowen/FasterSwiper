#pragma once

#include <cstdint>

#include "src/engine/position-reporter.h"

namespace fasterswiper {

class MovementEngine {
public:
  virtual ~MovementEngine() = default;

  virtual int64_t position() const = 0;

  virtual void SetPosition(int64_t new_position) = 0;

  virtual void Commit() = 0;
};

class ContinuousMovementEngine : public MovementEngine {
public:
  explicit ContinuousMovementEngine(AxisAdapter *absl_nonnull axis_adapter);

  int64_t position() const override { return current_position_; }

  void SetPosition(int64_t new_position) override;

  void Commit() override;

private:
  const AxisAdapter *axis_adapter_ = nullptr;

  bool gesture_started_ = false;
  int64_t origin_position_ = 0;
  int64_t current_position_ = 0;
  std::optional<int64_t> deferred_position_;
  int64_t latest_direction_ = 0;

  [[nodiscard]] int64_t distance_from_origin() const;
  [[nodiscard]] double progress_from_origin() const;

  void Reset();
  void PostEvent(int phase, double progress,
                 std::optional<double> velocity = std::nullopt) const;
};

class SegmentedMovementEngine : public MovementEngine {
public:
  explicit SegmentedMovementEngine(AxisAdapter *absl_nonnull axis_adapter);

  int64_t position() const override { return current_position_; }

  void SetPosition(int64_t new_position) override;

  void Commit() override;

private:
  const AxisAdapter *axis_adapter_ = nullptr;

  bool gesture_started_ = false;
  int64_t overall_origin_position_ = 0;
  int64_t origin_position_ = 0;
  int64_t current_position_ = 0;

  void Reset();
  int64_t GetNextBoundary(bool moving_right) const;
  void PostEvent(int phase, double progress,
                 std::optional<double> velocity = std::nullopt) const;
};

} // namespace fasterswiper
