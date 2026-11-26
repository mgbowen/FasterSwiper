#include "src/engine/swipe-animator.h"

#include "src/animation-timer.h"
#include "src/compatibility.h"
#include "src/easing.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/const.h"
#include "src/engine/space-switch-operation.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/time/time.h>
#include <gtest/gtest.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

class CountingCGEventSink : public CGEventSink {
public:
  void Post(CGEventRef absl_nonnull /*event*/) override { ++post_count_; }
  int post_count() const { return post_count_; }

private:
  int post_count_ = 0;
};

class FakeAxisAdapter : public AxisAdapter {
public:
  FakeAxisAdapter(Axis axis, int64_t initial_position,
                  std::pair<int64_t, int64_t> soft_limits)
      : axis_(axis),
        committed_position_(initial_position),
        soft_limits_(soft_limits) {}

  constexpr absl::string_view debug_name() const override {
    return "FakeAxisAdapter";
  }

  Axis movement_direction() const override { return axis_; }

  double NanoswipesToProgress(int64_t nanoswipes) const override {
    return static_cast<double>(nanoswipes) / kOneSwipeInNanoswipes;
  }

  int64_t ProgressToNanoswipes(double progress) const override {
    return static_cast<int64_t>(std::llround(progress * kOneSwipeInNanoswipes));
  }

  bool WaitForCommittedPositionChanged(
      int64_t /*original_position*/, int64_t /*target_position*/,
      absl::Duration /*deadline*/) const override {
    return true;
  }

  absl::StatusOr<int64_t> committed_position() const override {
    return committed_position_;
  }

  std::pair<int64_t, int64_t> position_soft_limits() const override {
    return soft_limits_;
  }

private:
  Axis axis_;
  int64_t committed_position_;
  std::pair<int64_t, int64_t> soft_limits_;
};

class FakeAnimationTimer : public AnimationTimer {
public:
  struct Record {
    std::optional<CGDirectDisplayID> display_id;
    absl::Duration duration = absl::ZeroDuration();
    bool fire_immediately = false;
    bool cancelled = false;
    bool finished = false;
  };

  FakeAnimationTimer(AnimationTimerParameters params, Record* record,
                     FakeAnimationTimer** active_slot)
      : params_(std::move(params)), record_(record), active_slot_(active_slot) {
    record_->display_id = params_.display_id;
    record_->duration = params_.duration;
    record_->fire_immediately = params_.fire_immediately;
    *active_slot_ = this;
  }

  ~FakeAnimationTimer() override {
    CancelImpl();
    if (*active_slot_ == this) {
      *active_slot_ = nullptr;
    }
  }

  void Cancel() override { CancelImpl(); }

  void FireTick(int64_t elapsed_ns) {
    if (stopped_) {
      return;
    }
    if (params_.tick_callback(elapsed_ns) == AnimationTickResult::kFinish) {
      stopped_ = true;
      record_->finished = true;
      std::move(params_.stopped_callback)(AnimationStopReason::kFinished);
    }
  }

private:
  void CancelImpl() {
    if (stopped_) {
      return;
    }
    stopped_ = true;
    record_->cancelled = true;
    std::move(params_.stopped_callback)(AnimationStopReason::kCancelled);
  }

  AnimationTimerParameters params_;
  Record* record_;
  FakeAnimationTimer** active_slot_;
  bool stopped_ = false;
};

struct FakeTimerHarness {
  std::vector<std::unique_ptr<FakeAnimationTimer::Record>> records;
  FakeAnimationTimer* active_timer = nullptr;

  AnimationTimerFactory MakeFactory() {
    return [this](AnimationTimerParameters params)
               -> std::unique_ptr<AnimationTimer> {
      records.push_back(std::make_unique<FakeAnimationTimer::Record>());
      return std::make_unique<FakeAnimationTimer>(
          std::move(params), records.back().get(), &active_timer);
    };
  }
};

TEST(SwipeAnimatorTest, ForwardsDisplayIdDurationAndFireImmediately) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  constexpr CGDirectDisplayID kDisplayId = 77;

  // Non-zero horizontal animation: fire_immediately is false.
  {
    FakeTimerHarness harness;
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kHorizontal, 0,
            std::make_pair(int64_t{0}, 2 * kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());

    SwipeAnimator animator(*std::move(maybe_op), kDisplayId,
                           harness.MakeFactory());
    ASSERT_TRUE(animator
                    .AnimateToPosition(
                        {
                            .target_position = kOneSwipeInNanoswipes,
                            .duration = absl::Milliseconds(200),
                            .easing_function = MakeEasingFunctionLinear(),
                        },
                        std::make_unique<CountingCGEventSink>())
                    .ok());

    ASSERT_EQ(harness.records.size(), 1u);
    EXPECT_EQ(harness.records[0]->display_id, kDisplayId);
    EXPECT_EQ(harness.records[0]->duration, absl::Milliseconds(200));
    EXPECT_FALSE(harness.records[0]->fire_immediately);
  }

  // Zero-duration horizontal animation: fire_immediately is forced true.
  {
    FakeTimerHarness harness;
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kHorizontal, 0,
            std::make_pair(int64_t{0}, 2 * kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());

    SwipeAnimator animator(*std::move(maybe_op), kDisplayId,
                           harness.MakeFactory());
    ASSERT_TRUE(animator
                    .AnimateToPosition(
                        {
                            .target_position = kOneSwipeInNanoswipes,
                            .duration = absl::ZeroDuration(),
                            .easing_function = MakeEasingFunctionLinear(),
                        },
                        std::make_unique<CountingCGEventSink>())
                    .ok());

    ASSERT_EQ(harness.records.size(), 1u);
    EXPECT_TRUE(harness.records[0]->fire_immediately);
  }

  // Compensated vertical animation: fire_immediately is true even for non-zero
  // duration.
  {
    FakeTimerHarness harness;
    int64_t fake_now_ns = 1'000'000'000LL;
    auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, 0,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return fake_now_ns; });
    ASSERT_TRUE(maybe_op.ok());

    SwipeAnimator animator(*std::move(maybe_op), kDisplayId,
                           harness.MakeFactory());
    ASSERT_TRUE(animator
                    .AnimateToPosition(
                        {
                            .target_position = kOneSwipeInNanoswipes,
                            .duration = absl::Milliseconds(200),
                            .easing_function = MakeEasingFunctionLinear(),
                        },
                        std::make_unique<CountingCGEventSink>())
                    .ok());

    ASSERT_EQ(harness.records.size(), 1u);
    EXPECT_EQ(harness.records[0]->display_id, kDisplayId);
    EXPECT_TRUE(harness.records[0]->fire_immediately);
  }
}

TEST(SwipeAnimatorTest, AnimatesToTargetAndCommitsOnFinish) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  FakeTimerHarness harness;
  auto maybe_op = ContinuousSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kHorizontal, 0,
          std::make_pair(int64_t{0}, 2 * kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
  ASSERT_TRUE(maybe_op.ok());

  SwipeAnimator animator(*std::move(maybe_op), std::nullopt,
                         harness.MakeFactory());
  ASSERT_TRUE(animator
                  .AnimateToPosition(
                      {
                          .target_position = kOneSwipeInNanoswipes,
                          .duration = absl::Milliseconds(200),
                          .easing_function = MakeEasingFunctionLinear(),
                      },
                      std::make_unique<CountingCGEventSink>())
                  .ok());

  ASSERT_NE(harness.active_timer, nullptr);
  EXPECT_FALSE(animator.is_committed());

  harness.active_timer->FireTick(100'000'000LL);
  EXPECT_EQ(animator.position(), kOneSwipeInNanoswipes / 2);
  EXPECT_FALSE(animator.is_committed());

  // Large penultimate gap (0.5 spaces > 0.05) holds at target for 1 extra tick.
  harness.active_timer->FireTick(200'000'000LL);
  EXPECT_EQ(animator.position(), kOneSwipeInNanoswipes);
  EXPECT_FALSE(animator.is_committed());

  harness.active_timer->FireTick(216'666'667LL);
  EXPECT_EQ(animator.position(), kOneSwipeInNanoswipes);
  EXPECT_TRUE(animator.is_committed());
  EXPECT_TRUE(harness.records[0]->finished);

  CountingCGEventSink sink;
  EXPECT_EQ(animator.SetPosition(0, &sink).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(animator
                .AnimateToPosition(
                    {
                        .target_position = 0,
                        .duration = absl::Milliseconds(100),
                        .easing_function = MakeEasingFunctionLinear(),
                    },
                    std::make_unique<CountingCGEventSink>())
                .code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(SwipeAnimatorTest, CancelMidAnimationPreservesPositionAndAllowsRetarget) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  FakeTimerHarness harness;
  auto maybe_op = ContinuousSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kHorizontal, 0,
          std::make_pair(int64_t{0}, 2 * kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
  ASSERT_TRUE(maybe_op.ok());

  SwipeAnimator animator(*std::move(maybe_op), std::nullopt,
                         harness.MakeFactory());
  ASSERT_TRUE(animator
                  .AnimateToPosition(
                      {
                          .target_position = kOneSwipeInNanoswipes,
                          .duration = absl::Milliseconds(200),
                          .easing_function = MakeEasingFunctionLinear(),
                      },
                      std::make_unique<CountingCGEventSink>())
                  .ok());

  harness.active_timer->FireTick(100'000'000LL);
  EXPECT_EQ(animator.position(), kOneSwipeInNanoswipes / 2);

  EXPECT_EQ(animator.CancelAnimation(),
            AnimatedSpaceSwitchOperationResult::kCancelled);
  EXPECT_FALSE(animator.is_committed());
  EXPECT_TRUE(harness.records[0]->cancelled);
  EXPECT_EQ(animator.position(), kOneSwipeInNanoswipes / 2);

  // Retargeting starts from the preserved midpoint.
  ASSERT_TRUE(animator
                  .AnimateToPosition(
                      {
                          .target_position = 2 * kOneSwipeInNanoswipes,
                          .duration = absl::Milliseconds(200),
                          .easing_function = MakeEasingFunctionLinear(),
                      },
                      std::make_unique<CountingCGEventSink>())
                  .ok());
  ASSERT_EQ(harness.records.size(), 2u);
  harness.active_timer->FireTick(100'000'000LL);
  EXPECT_EQ(animator.position(), 1'250'000);
  harness.active_timer->FireTick(200'000'000LL);
  EXPECT_EQ(animator.position(), 2 * kOneSwipeInNanoswipes);
  EXPECT_FALSE(animator.is_committed());
  harness.active_timer->FireTick(216'666'667LL);
  EXPECT_EQ(animator.position(), 2 * kOneSwipeInNanoswipes);
  EXPECT_TRUE(animator.is_committed());
}

TEST(SwipeAnimatorTest, SetPositionCancelsActiveAnimation) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  FakeTimerHarness harness;
  auto maybe_op = ContinuousSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kHorizontal, 0,
          std::make_pair(int64_t{0}, 2 * kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
  ASSERT_TRUE(maybe_op.ok());

  SwipeAnimator animator(*std::move(maybe_op), std::nullopt,
                         harness.MakeFactory());
  ASSERT_TRUE(animator
                  .AnimateToPosition(
                      {
                          .target_position = kOneSwipeInNanoswipes,
                          .duration = absl::Milliseconds(200),
                          .easing_function = MakeEasingFunctionLinear(),
                      },
                      std::make_unique<CountingCGEventSink>())
                  .ok());

  harness.active_timer->FireTick(50'000'000LL);

  CountingCGEventSink sink;
  ASSERT_TRUE(animator.SetPosition(300'000, &sink).ok());
  EXPECT_TRUE(harness.records[0]->cancelled);
  EXPECT_EQ(animator.position(), 300'000);
  EXPECT_GT(sink.post_count(), 0);
}

}  // namespace
}  // namespace fasterswiper
