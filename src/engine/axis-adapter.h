#pragma once

#include "src/macos-private.h"
#include "src/space-state.h"

#include <cstdint>
#include <utility>

#include <absl/status/statusor.h>

namespace fasterswiper {

enum class Axis {
  kHorizontal = kCGGestureMotionHorizontal,
  kVertical = kCGGestureMotionVertical,
};

class AxisAdapter {
public:
  virtual ~AxisAdapter() = default;

  [[nodiscard]] virtual Axis movement_direction() const = 0;

  [[nodiscard]] virtual double
  NanoswipesToProgress(int64_t nanoswipes) const = 0;

  [[nodiscard]] virtual int64_t ProgressToNanoswipes(double progress) const = 0;

  [[nodiscard]] virtual bool
  WaitForCommittedPositionChanged(int64_t original_position,
                                  absl::Duration deadline) const = 0;

  [[nodiscard]] virtual absl::StatusOr<int64_t> committed_position() const = 0;

  [[nodiscard]] virtual std::pair<int64_t, int64_t>
  position_soft_limits() const = 0;
};

class HorizontalAxisAdapter : public AxisAdapter {
public:
  HorizontalAxisAdapter(SpaceState space_state);
  ~HorizontalAxisAdapter() override = default;

  [[nodiscard]] Axis movement_direction() const override {
    return Axis::kHorizontal;
  }

  [[nodiscard]] double NanoswipesToProgress(int64_t nanoswipes) const override;

  [[nodiscard]] int64_t ProgressToNanoswipes(double progress) const override;

  [[nodiscard]] bool
  WaitForCommittedPositionChanged(int64_t original_position,
                                  absl::Duration deadline) const override;

  [[nodiscard]] absl::StatusOr<int64_t> committed_position() const override;

  [[nodiscard]] std::pair<int64_t, int64_t>
  position_soft_limits() const override;

private:
  const SpaceState space_state_;
};

class VerticalAxisAdapter : public AxisAdapter {
public:
  VerticalAxisAdapter() = default;
  ~VerticalAxisAdapter() override = default;

  [[nodiscard]] Axis movement_direction() const override {
    return Axis::kVertical;
  }

  [[nodiscard]] double NanoswipesToProgress(int64_t nanoswipes) const override;

  [[nodiscard]] int64_t ProgressToNanoswipes(double progress) const override;

  [[nodiscard]] bool
  WaitForCommittedPositionChanged(int64_t original_position,
                                  absl::Duration deadline) const override;

  [[nodiscard]] absl::StatusOr<int64_t> committed_position() const override;

  [[nodiscard]] std::pair<int64_t, int64_t>
  position_soft_limits() const override;
};

} // namespace fasterswiper
