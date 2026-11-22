#pragma once

#include "src/easing.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/deferred-position.h"
#include "src/engine/vertical-spring-compensator.h"
#include "src/periodic-timer.h"

#include <optional>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/base/nullability.h>
#include <absl/functional/any_invocable.h>
#include <absl/status/statusor.h>
#include <absl/time/time.h>

namespace fasterswiper {

class CGEventSink {
public:
  CGEventSink() = default;
  virtual ~CGEventSink() = default;
  CGEventSink(const CGEventSink&) = default;
  CGEventSink& operator=(const CGEventSink&) = default;
  CGEventSink(CGEventSink&&) = default;
  CGEventSink& operator=(CGEventSink&&) = default;

  virtual void Post(CGEventRef absl_nonnull event) = 0;
};

class CGEventPostSink : public CGEventSink {
public:
  void Post(CGEventRef absl_nonnull event) override;
};

class CGEventTapPostEventSink : public CGEventSink {
public:
  explicit CGEventTapPostEventSink(CGEventTapProxy absl_nonnull proxy)
      : proxy_(proxy) {}
  void Post(CGEventRef absl_nonnull event) override;

private:
  CGEventTapProxy absl_nonnull proxy_;
};

class SpaceSwitchOperation {
public:
  enum class AnimationStepResult {
    kContinue,
    kFinished,
  };

  struct Options {
    std::optional<bool> natural_scrolling_enabled;

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Options& options) {
      absl::Format(
          &sink, "SpaceSwitchOperation::Options{natural_scrolling_enabled=%v}",
          OptionalToString(options.natural_scrolling_enabled));
    }
  };

  explicit SpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter)
      : SpaceSwitchOperation(std::move(axis_adapter), Options{}) {}

  explicit SpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter,
                                Options options);

  virtual ~SpaceSwitchOperation();

  // Non-copyable, non-movable.
  SpaceSwitchOperation(const SpaceSwitchOperation&) = delete;
  SpaceSwitchOperation& operator=(const SpaceSwitchOperation&) = delete;
  SpaceSwitchOperation(SpaceSwitchOperation&&) = delete;
  SpaceSwitchOperation& operator=(SpaceSwitchOperation&&) = delete;

  [[nodiscard]] virtual constexpr absl::string_view debug_name() const = 0;

  [[nodiscard]] const Options& options() const { return options_; }

  [[nodiscard]] const AxisAdapter& axis_adapter() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] int64_t position() const ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limits() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetPosition(int64_t new_position, CGEventSink* absl_nonnull event_sink)
      ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] virtual bool FiresAnimationTimerImmediately() const {
    return false;
  }

  [[nodiscard]] AnimationStepResult StepAnimation(
      int64_t start_position, int64_t target_position,
      int64_t time_since_start_ns, absl::Duration duration,
      const EasingFunction& easing, CGEventSink* absl_nonnull event_sink)
      ABSL_LOCKS_EXCLUDED(mutex_);

  void Commit(CGEventSink* absl_nonnull event_sink) ABSL_LOCKS_EXCLUDED(mutex_);

protected:
  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limits_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  [[nodiscard]] virtual int64_t position_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_) = 0;
  virtual void SetPositionLocked(int64_t new_position,
                                 CGEventSink* absl_nonnull event_sink)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;
  [[nodiscard]] virtual AnimationStepResult StepAnimationLocked(
      int64_t start_position, int64_t target_position,
      int64_t time_since_start_ns, absl::Duration duration,
      const EasingFunction& easing, CGEventSink* absl_nonnull event_sink)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  virtual void CommitLocked(CGEventSink* absl_nonnull event_sink)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

  [[nodiscard]] const AxisAdapter& axis_adapter_locked() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void PostEvent(CGEventSink* absl_nonnull event_sink, int phase,
                 double progress,
                 std::optional<double> velocity = std::nullopt) const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  [[nodiscard]] const absl::Mutex* absl_nonnull mutex() const
      ABSL_LOCK_RETURNED(mutex_);

private:
  const std::unique_ptr<AxisAdapter> axis_adapter_;
  const Options options_;

  mutable absl::Mutex mutex_;
  bool is_committed_ ABSL_GUARDED_BY(mutex_) = false;
};

class ContinuousSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  static absl::StatusOr<std::unique_ptr<ContinuousSpaceSwitchOperation>> Create(
      std::unique_ptr<AxisAdapter> axis_adapter, Options options = Options{});

  constexpr absl::string_view debug_name() const override {
    return "ContinuousSpaceSwitchOperation";
  }

private:
  const int64_t origin_position_ = 0;

  bool gesture_started_ = false;
  DeferredPosition current_position_;
  int64_t latest_direction_ = 0;

  ContinuousSpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter,
                                 int64_t origin_position,
                                 Options options = Options{});

  [[nodiscard]] int64_t distance_from_origin() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex());
  [[nodiscard]] double progress_from_origin() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex());

  [[nodiscard]] int64_t position_locked() const override
      ABSL_SHARED_LOCKS_REQUIRED(mutex());
  void SetPositionLocked(int64_t new_position,
                         CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  void CommitLocked(CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
};

class SegmentedSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  static absl::StatusOr<std::unique_ptr<SegmentedSpaceSwitchOperation>> Create(
      std::unique_ptr<AxisAdapter> axis_adapter, Options options = Options{});

  constexpr absl::string_view debug_name() const override {
    return "SegmentedSpaceSwitchOperation";
  }

private:
  const int64_t operation_origin_position_ = 0;
  DeferredPosition current_position_;

  struct States {
    struct Idle {
      template <typename Sink>
      friend void AbslStringify(Sink& sink, const Idle& state) {
        absl::Format(&sink, "Idle{}");
      }
    };

    struct GestureActive {
      int64_t origin_position = 0;

      template <typename Sink>
      friend void AbslStringify(Sink& sink, const GestureActive& state) {
        absl::Format(&sink, "GestureActive{origin_position=%d}",
                     state.origin_position);
      }
    };
  };

  using State = std::variant<States::Idle, States::GestureActive>;
  State state_ = States::Idle{};

  SegmentedSpaceSwitchOperation(std::unique_ptr<AxisAdapter> axis_adapter,
                                int64_t operation_origin_position,
                                Options options = Options{});

  static std::string StateToString(const State& state);

  [[nodiscard]] int64_t position_locked() const override
      ABSL_SHARED_LOCKS_REQUIRED(mutex());
  void SetPositionLocked(int64_t new_position,
                         CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  void CommitLocked(CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());

  void SetState(State new_state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  void EndGesture(const States::GestureActive& gesture_active,
                  CGEventSink* absl_nonnull event_sink)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  int64_t GetNextBoundary(bool is_moving_positive)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
};

class CompensatedVerticalSpaceSwitchOperation : public SpaceSwitchOperation {
public:
  using ClockFn = absl::AnyInvocable<int64_t() const>;

  static absl::StatusOr<
      std::unique_ptr<CompensatedVerticalSpaceSwitchOperation>>
  Create(std::unique_ptr<AxisAdapter> axis_adapter, Options options = Options{},
         ClockFn clock = UptimeInNanoseconds);

  constexpr absl::string_view debug_name() const override {
    return "CompensatedVerticalSpaceSwitchOperation";
  }

  [[nodiscard]] bool FiresAnimationTimerImmediately() const override {
    return true;
  }

private:
  const int64_t operation_origin_position_ = 0;
  ClockFn clock_;
  int64_t logical_position_ = 0;
  std::optional<int64_t> intended_target_position_;
  VerticalSpringCompensator compensator_;

  struct States {
    struct Idle {};
    struct GestureActive {
      int64_t origin_position = 0;
      int64_t direction = 1;  // +1 or -1
      bool is_rubberbanding = false;
    };
  };

  using State = std::variant<States::Idle, States::GestureActive>;
  State state_ = States::Idle{};

  CompensatedVerticalSpaceSwitchOperation(
      std::unique_ptr<AxisAdapter> axis_adapter,
      int64_t operation_origin_position, Options options, ClockFn clock);

  [[nodiscard]] int64_t position_locked() const override
      ABSL_SHARED_LOCKS_REQUIRED(mutex());
  void SetPositionLocked(int64_t new_position,
                         CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  [[nodiscard]] AnimationStepResult StepAnimationLocked(
      int64_t start_position, int64_t target_position,
      int64_t time_since_start_ns, absl::Duration duration,
      const EasingFunction& easing,
      CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  void CommitLocked(CGEventSink* absl_nonnull event_sink) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());

  void StartSegmentLocked(int64_t dir, int64_t now_ns,
                          CGEventSink* absl_nonnull event_sink)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
  void EndSegmentLocked(int64_t final_segment_position, int64_t now_ns,
                        CGEventSink* absl_nonnull event_sink)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex());
};

}  // namespace fasterswiper
