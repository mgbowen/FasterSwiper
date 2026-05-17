#pragma once

#include "src/engine/axis-adapter.h"
#include "src/engine/deferred-position.h"

#include <absl/status/statusor.h>

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

  [[nodiscard]] virtual constexpr absl::string_view debug_name() const = 0;

  [[nodiscard]] const AxisAdapter &axis_adapter() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] int64_t position() const ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limits() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetPosition(int64_t new_position) ABSL_LOCKS_EXCLUDED(mutex_);

  void Commit() ABSL_LOCKS_EXCLUDED(mutex_);

protected:
  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limits_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  [[nodiscard]] virtual int64_t position_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_) = 0;
  virtual void SetPositionLocked(int64_t new_position)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;
  virtual void CommitLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

  [[nodiscard]] const AxisAdapter &axis_adapter_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void PostEvent(int phase, double progress,
                 std::optional<double> velocity = std::nullopt) const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  // Protected to allow for Abseil thread annotations, do not use in derived
  // classes.
  mutable absl::Mutex mutex_;

private:
  const std::unique_ptr<AxisAdapter> axis_adapter_;

  bool is_committed_ ABSL_GUARDED_BY(mutex_) = false;
};

class ContinuousSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  static absl::StatusOr<std::unique_ptr<ContinuousSpaceSwitchOperation>>
  Create(std::unique_ptr<AxisAdapter> axis_adapter);

  constexpr absl::string_view debug_name() const override {
    return "ContinuousSpaceSwitchOperation";
  }

private:
  const int64_t origin_position_ = 0;

  bool gesture_started_ = false;
  DeferredPosition current_position_;
  int64_t latest_direction_ = 0;

  ContinuousSpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter,
                                 int64_t origin_position);

  [[nodiscard]] int64_t distance_from_origin() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);
  [[nodiscard]] double progress_from_origin() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  [[nodiscard]] int64_t position_locked() const override
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);
  void SetPositionLocked(int64_t new_position) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void CommitLocked() override ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
};

class SegmentedSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  static absl::StatusOr<std::unique_ptr<SegmentedSpaceSwitchOperation>>
  Create(std::unique_ptr<AxisAdapter> axis_adapter);

  constexpr absl::string_view debug_name() const override {
    return "SegmentedSpaceSwitchOperation";
  }

private:
  const int64_t operation_origin_position_ = 0;
  DeferredPosition current_position_;

  struct States {
    struct Idle {
      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Idle &state) {
        absl::Format(&sink, "Idle{}");
      }
    };

    struct GestureActive {
      int64_t origin_position = 0;

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const GestureActive &state) {
        absl::Format(&sink, "GestureActive{origin_position=%d}",
                     state.origin_position);
      }
    };
  };

  using State = std::variant<States::Idle, States::GestureActive>;
  State state_ = States::Idle{};

  SegmentedSpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter,
                                int64_t operation_origin_position);

  static std::string StateToString(const State &state);

  [[nodiscard]] int64_t position_locked() const override
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);
  void SetPositionLocked(int64_t new_position) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void CommitLocked() override ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void SetState(State new_state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void EndGesture(const States::GestureActive &gesture_active)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  int64_t GetNextBoundary(bool is_moving_positive)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
};

} // namespace fasterswiper
