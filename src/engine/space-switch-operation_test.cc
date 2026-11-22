#include "src/engine/space-switch-operation.h"

#include "src/compatibility.h"
#include "src/easing.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/const.h"
#include "src/event.h"
#include "src/fp1616.h"
#include "src/macos-private.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <absl/status/statusor.h>
#include <absl/time/time.h>
#include <gtest/gtest.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

struct RecordedEvent {
  int phase = 0;
  double progress = 0.0;
  double velocity_y = 0.0;
  double velocity_x = 0.0;
};

class RecordingCGEventSink : public CGEventSink {
public:
  void Post(CGEventRef absl_nonnull event) override {
    events_.push_back(RecordedEvent{
        .phase = static_cast<int>(
            CGEventGetIntegerValueField(event, kCGEventGesturePhase)),
        .progress =
            CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress),
        .velocity_y =
            CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY),
        .velocity_x =
            CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX),
    });
  }

  const std::vector<RecordedEvent>& events() const { return events_; }

private:
  std::vector<RecordedEvent> events_;
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
      int64_t /*original_position*/,
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

// Checks wire invariants across compensated segments:
// 1. Every segment starts with exactly one kGestureBegan and ends with exactly
//    one terminal event (kGestureEnded or kGestureCancelled).
// 2. Every kGestureChanged has |progress| in [kFixedFP1616Lsb, 1.0].
void AssertCompensatedSegmentInvariants(
    const std::vector<RecordedEvent>& events, int expected_segments) {
  bool in_segment = false;
  int segments = 0;
  for (const auto& ev : events) {
    if (ev.phase == kGestureBegan) {
      EXPECT_FALSE(in_segment) << "Nested kGestureBegan without terminal event";
      in_segment = true;
      ++segments;
    } else if (ev.phase == kGestureChanged) {
      EXPECT_TRUE(in_segment) << "kGestureChanged outside active segment";
      EXPECT_GE(std::abs(ev.progress), kFixedFP1616Lsb - 1e-12);
      EXPECT_LE(std::abs(ev.progress), 1.0 + 1e-12);
    } else if (ev.phase == kGestureEnded || ev.phase == kGestureCancelled) {
      EXPECT_TRUE(in_segment)
          << "Terminal event without matching kGestureBegan";
      in_segment = false;
    } else {
      ADD_FAILURE() << "Unexpected gesture phase: " << ev.phase;
    }
  }
  EXPECT_FALSE(in_segment) << "Unterminated segment";
  EXPECT_EQ(segments, expected_segments);
}

TEST(SpaceSwitchOperationTest, BaseStepAnimationMatchesMainGoldenParity) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  const EasingFunction quad = MakeEasingFunctionEaseOutQuadratic();
  const absl::Duration duration = absl::Milliseconds(200);
  const std::vector<int64_t> ticks_ns = {
      0,           4'166'667,   25'000'000,  75'000'000,
      150'000'000, 195'000'000, 200'000'000,
  };

  auto check_parity = [&](auto create_op, int64_t start_pos,
                          int64_t target_pos) {
    auto op_via_step = create_op();
    auto op_golden = create_op();
    RecordingCGEventSink sink_step;
    RecordingCGEventSink sink_golden;

    for (int64_t t_ns : ticks_ns) {
      const auto step_res = op_via_step->StepAnimation(
          start_pos, target_pos, t_ns, duration, quad, &sink_step);

      // Exact main SwipeAnimator interpolation formula.
      const double total_ns = absl::ToDoubleNanoseconds(duration);
      const double linear_t =
          total_ns <= 0
              ? 1.0
              : std::clamp(static_cast<double>(t_ns) / total_ns, 0.0, 1.0);
      const double eased_t = quad(linear_t);
      const int64_t interpolated =
          start_pos +
          static_cast<int64_t>(std::round((target_pos - start_pos) * eased_t));
      const bool finished = linear_t >= 1.0 || interpolated == target_pos;
      op_golden->SetPosition(finished ? target_pos : interpolated,
                             &sink_golden);

      EXPECT_EQ(
          step_res == SpaceSwitchOperation::AnimationStepResult::kFinished,
          finished);
      if (finished) {
        break;
      }
    }

    op_via_step->Commit(&sink_step);
    op_golden->Commit(&sink_golden);

    ASSERT_EQ(sink_step.events().size(), sink_golden.events().size());
    for (std::size_t i = 0; i < sink_step.events().size(); ++i) {
      EXPECT_EQ(sink_step.events()[i].phase, sink_golden.events()[i].phase);
      EXPECT_DOUBLE_EQ(sink_step.events()[i].progress,
                       sink_golden.events()[i].progress);
      EXPECT_DOUBLE_EQ(sink_step.events()[i].velocity_y,
                       sink_golden.events()[i].velocity_y);
      EXPECT_DOUBLE_EQ(sink_step.events()[i].velocity_x,
                       sink_golden.events()[i].velocity_x);
    }
  };

  const SpaceSwitchOperation::Options opts{.natural_scrolling_enabled = false};
  check_parity(
      [&] {
        return *ContinuousSpaceSwitchOperation::Create(
            std::make_unique<FakeAxisAdapter>(
                Axis::kHorizontal, 0,
                std::make_pair(0, 3 * kOneSwipeInNanoswipes)),
            opts);
      },
      0, kOneSwipeInNanoswipes);

  check_parity(
      [&] {
        return *SegmentedSpaceSwitchOperation::Create(
            std::make_unique<FakeAxisAdapter>(
                Axis::kVertical, 0,
                std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
            opts);
      },
      0, kOneSwipeInNanoswipes);
}

TEST(CompensatedVerticalOperationTest,
     FullStrokeDragHoldsBoundaryAndSettlesOnRelease) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  int64_t fake_now_ns = 1'000'000'000LL;
  auto clock = [&] { return fake_now_ns; };

  // Desktop (0) -> MC (+1e6), soft limits [-1e6, +1e6]: no further segment past
  // +1e6.
  auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kVertical, 0,
          std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false}, clock);
  ASSERT_TRUE(maybe_op.ok()) << maybe_op.status();
  auto op = *std::move(maybe_op);
  EXPECT_TRUE(op->FiresAnimationTimerImmediately());

  RecordingCGEventSink sink;

  // Fast drag reaching +1.0e6 and overshooting to +1.1e6.
  fake_now_ns += 8'000'000LL;
  op->SetPosition(500'000, &sink);
  fake_now_ns += 8'000'000LL;
  op->SetPosition(1'000'000, &sink);
  fake_now_ns += 8'000'000LL;
  op->SetPosition(1'100'000, &sink);

  // Must remain GestureActive with zero premature kGestureEnded events.
  for (const auto& ev : sink.events()) {
    EXPECT_NE(ev.phase, kGestureEnded);
    EXPECT_NE(ev.phase, kGestureCancelled);
  }

  // Lagged visual position must be strictly < +1e6 so finger release runs
  // StepAnimation + settle tail.
  const int64_t release_start = op->position();
  EXPECT_LT(release_start, kOneSwipeInNanoswipes);

  // Release runs StepAnimation and settles cleanly before Commit.
  const EasingFunction linear = MakeEasingFunctionLinear();
  const absl::Duration dur = absl::Milliseconds(100);
  constexpr int64_t kDtNs = 1'000'000'000LL / 240;
  int64_t elapsed_ns = 0;
  bool finished = false;
  for (int tick = 0; tick <= 120; ++tick) {
    const auto res = op->StepAnimation(release_start, kOneSwipeInNanoswipes,
                                       elapsed_ns, dur, linear, &sink);
    if (res == SpaceSwitchOperation::AnimationStepResult::kFinished) {
      finished = true;
      break;
    }
    fake_now_ns += kDtNs;
    elapsed_ns += kDtNs;
  }
  EXPECT_TRUE(finished);
  EXPECT_NEAR(static_cast<double>(op->position()) / kOneSwipeInNanoswipes, 1.0,
              1.2e-4);

  op->Commit(&sink);
  EXPECT_EQ(op->position(), kOneSwipeInNanoswipes);
  AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);

  // Sustained hold past end_boundary converges cleanly to +1.0e6 and commits at
  // end_boundary without spawning a rubberband segment.
  {
    int64_t hold_now_ns = 2'000'000'000LL;
    auto maybe_hold_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, 0,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return hold_now_ns; });
    ASSERT_TRUE(maybe_hold_op.ok());
    auto hold_op = *std::move(maybe_hold_op);
    RecordingCGEventSink hold_sink;

    for (int i = 0; i < 3; ++i) {
      hold_now_ns += 500'000'000LL;
      hold_op->SetPosition(1'150'000, &hold_sink);
      EXPECT_LE(hold_op->position(), kOneSwipeInNanoswipes);
    }
    hold_now_ns += 500'000'000LL;
    EXPECT_EQ(hold_op->position(), kOneSwipeInNanoswipes);

    hold_op->Commit(&hold_sink);
    EXPECT_EQ(hold_op->position(), kOneSwipeInNanoswipes);
    AssertCompensatedSegmentInvariants(hold_sink.events(),
                                       /*expected_segments=*/1);
  }

  // Multi-segment drag: MC (+1e6) -> Desktop (0) -> AE (-0.4e6) holds at exact
  // 0, then splits cleanly when crossing strictly past 0 into segment 2.
  {
    int64_t multi_now_ns = 3'000'000'000LL;
    auto maybe_multi = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, kOneSwipeInNanoswipes,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return multi_now_ns; });
    ASSERT_TRUE(maybe_multi.ok());
    auto multi = *std::move(maybe_multi);
    RecordingCGEventSink multi_sink;

    multi_now_ns += 8'000'000LL;
    multi->SetPosition(500'000, &multi_sink);
    multi_now_ns += 8'000'000LL;
    multi->SetPosition(0, &multi_sink);
    for (const auto& ev : multi_sink.events()) {
      EXPECT_NE(ev.phase, kGestureEnded);
    }

    multi_now_ns += 8'000'000LL;
    multi->SetPosition(-400'000, &multi_sink);
    multi->Commit(&multi_sink);
    AssertCompensatedSegmentInvariants(multi_sink.events(),
                                       /*expected_segments=*/2);
  }
}

TEST(CompensatedVerticalOperationTest,
     MidFlightRegrabDoesNotDropCommandBelowVisualProgress) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  int64_t fake_now_ns = 1'000'000'000LL;
  auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kVertical, 0,
          std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
      [&] { return fake_now_ns; });
  ASSERT_TRUE(maybe_op.ok());
  auto op = *std::move(maybe_op);
  RecordingCGEventSink sink;

  const EasingFunction quint = MakeEasingFunctionEaseOutQuintic();
  constexpr int64_t kDtNs = 1'000'000'000LL / 240;
  for (int tick = 0; tick < 12; ++tick) {
    const auto res = op->StepAnimation(0, kOneSwipeInNanoswipes, tick * kDtNs,
                                       absl::Milliseconds(200), quint, &sink);
    ASSERT_EQ(res, SpaceSwitchOperation::AnimationStepResult::kContinue);
    fake_now_ns += kDtNs;
  }

  // Re-grab at the current visual position reported by op->position().
  const int64_t grab_pos = op->position();
  const double grab_progress =
      static_cast<double>(grab_pos) / kOneSwipeInNanoswipes;
  ASSERT_GT(grab_progress, 0.2);
  ASSERT_LT(grab_progress, 0.9);

  fake_now_ns += 4'000'000LL;
  op->SetPosition(grab_pos, &sink);
  ASSERT_FALSE(sink.events().empty());
  const RecordedEvent& regrab_ev = sink.events().back();
  EXPECT_EQ(regrab_ev.phase, kGestureChanged);
  // With r_prev_ = state_.y, forward velocity lead survives re-grab instead of
  // injecting a negative velocity spike below visual progress.
  EXPECT_GT(regrab_ev.progress, grab_progress + 0.01);

  op->Commit(&sink);
  AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
}

TEST(CompensatedVerticalOperationTest,
     RubberbandPassesRawProgressSkipsSettleTailAndCancels) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  // Case 1: Swiping up past already-open MC (+1e6 -> +1.4e6 -> +2.2e6).
  {
    int64_t fake_now_ns = 1'000'000'000LL;
    auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, kOneSwipeInNanoswipes,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return fake_now_ns; });
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    fake_now_ns += 8'000'000LL;
    op->SetPosition(1'400'000, &sink);
    fake_now_ns += 8'000'000LL;
    op->SetPosition(2'200'000, &sink);

    EXPECT_EQ(op->position(), 2'200'000);
    ASSERT_EQ(sink.events().size(), 3u);
    EXPECT_EQ(sink.events()[0].phase, kGestureBegan);
    EXPECT_EQ(sink.events()[1].phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events()[1].progress), 0.4f);
    EXPECT_EQ(sink.events()[2].phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events()[2].progress),
                    1.2f);  // Raw unclamped > 1.0.

    // Release skips settle tail: kFinished right at t == duration.
    const EasingFunction linear = MakeEasingFunctionLinear();
    const auto mid =
        op->StepAnimation(2'200'000, kOneSwipeInNanoswipes, 50'000'000LL,
                          absl::Milliseconds(100), linear, &sink);
    EXPECT_EQ(mid, SpaceSwitchOperation::AnimationStepResult::kContinue);
    const auto end =
        op->StepAnimation(2'200'000, kOneSwipeInNanoswipes, 100'000'000LL,
                          absl::Milliseconds(100), linear, &sink);
    EXPECT_EQ(end, SpaceSwitchOperation::AnimationStepResult::kFinished);

    op->Commit(&sink);
    EXPECT_EQ(op->position(), kOneSwipeInNanoswipes);
    EXPECT_EQ(sink.events().back().phase, kGestureCancelled);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(kEpsilon));
  }

  // Case 2: Swiping down on fullscreen space (soft_min = 0, 0 -> -0.6e6).
  {
    int64_t fake_now_ns = 1'000'000'000LL;
    auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, 0, std::make_pair(0, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return fake_now_ns; });
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    fake_now_ns += 8'000'000LL;
    op->SetPosition(-600'000, &sink);
    EXPECT_EQ(op->position(), -600'000);
    ASSERT_EQ(sink.events().size(), 2u);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events()[1].progress), -0.6f);

    const auto end =
        op->StepAnimation(-600'000, 0, 100'000'000LL, absl::Milliseconds(100),
                          MakeEasingFunctionLinear(), &sink);
    EXPECT_EQ(end, SpaceSwitchOperation::AnimationStepResult::kFinished);

    op->Commit(&sink);
    EXPECT_EQ(op->position(), 0);
    EXPECT_EQ(sink.events().back().phase, kGestureCancelled);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(-kEpsilon));
  }
}

TEST(CompensatedVerticalOperationTest, CancelSignConventionBothDirections) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  // Desktop 0 -> +0.3e6 -> cancel back to 0: emits +kEpsilon on changed &
  // cancelled.
  {
    int64_t fake_now_ns = 1'000'000'000LL;
    auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, 0,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return fake_now_ns; });
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    fake_now_ns += 8'000'000LL;
    op->SetPosition(300'000, &sink);
    op->Commit(&sink);

    AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
    ASSERT_GE(sink.events().size(), 3u);
    const auto& last_changed = sink.events()[sink.events().size() - 2];
    const auto& cancelled = sink.events().back();
    EXPECT_EQ(last_changed.phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(last_changed.progress),
                    static_cast<float>(kEpsilon));
    EXPECT_EQ(cancelled.phase, kGestureCancelled);
    EXPECT_FLOAT_EQ(static_cast<float>(cancelled.progress),
                    static_cast<float>(kEpsilon));
    EXPECT_FLOAT_EQ(static_cast<float>(cancelled.velocity_y),
                    static_cast<float>(kEpsilon));
  }

  // MC +1e6 -> +0.7e6 -> cancel back to +1e6 (dir = -1): emits -kEpsilon on
  // changed & cancelled.
  {
    int64_t fake_now_ns = 1'000'000'000LL;
    auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, kOneSwipeInNanoswipes,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return fake_now_ns; });
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    fake_now_ns += 8'000'000LL;
    op->SetPosition(700'000, &sink);
    op->Commit(&sink);

    AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
    ASSERT_GE(sink.events().size(), 3u);
    const auto& last_changed = sink.events()[sink.events().size() - 2];
    const auto& cancelled = sink.events().back();
    EXPECT_EQ(last_changed.phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(last_changed.progress),
                    static_cast<float>(-kEpsilon));
    EXPECT_EQ(cancelled.phase, kGestureCancelled);
    EXPECT_FLOAT_EQ(static_cast<float>(cancelled.progress),
                    static_cast<float>(-kEpsilon));
    EXPECT_FLOAT_EQ(static_cast<float>(cancelled.velocity_y),
                    static_cast<float>(-kEpsilon));
  }
}

TEST(CompensatedVerticalOperationTest, TimeoutPathEmitsSingleTerminalEvent) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  // Frozen clock (now_ns == 0) prevents dwell from ever maturing so the T +
  // 250 ms timeout path must terminate StepAnimation cleanly.
  auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kVertical, 0,
          std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
      []() -> int64_t { return 0; });
  ASSERT_TRUE(maybe_op.ok());
  auto op = *std::move(maybe_op);
  RecordingCGEventSink sink;

  const EasingFunction linear = MakeEasingFunctionLinear();
  const absl::Duration duration = absl::Milliseconds(200);

  EXPECT_EQ(op->StepAnimation(0, kOneSwipeInNanoswipes, 449'000'000LL, duration,
                              linear, &sink),
            SpaceSwitchOperation::AnimationStepResult::kContinue);
  EXPECT_EQ(op->StepAnimation(0, kOneSwipeInNanoswipes, 450'000'000LL, duration,
                              linear, &sink),
            SpaceSwitchOperation::AnimationStepResult::kFinished);

  op->Commit(&sink);
  AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
  EXPECT_EQ(sink.events().back().phase, kGestureEnded);
  EXPECT_DOUBLE_EQ(sink.events().back().progress, 1.0);
  EXPECT_DOUBLE_EQ(sink.events().back().velocity_y, 6.0);
}

TEST(CompensatedVerticalOperationTest, ZeroDurationFinishesImmediatelyOnTick0) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  int64_t fake_now_ns = 1'000'000'000LL;
  auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(
          Axis::kVertical, 0,
          std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
      [&] { return fake_now_ns; });
  ASSERT_TRUE(maybe_op.ok());
  auto op = *std::move(maybe_op);
  RecordingCGEventSink sink;

  const auto res =
      op->StepAnimation(0, kOneSwipeInNanoswipes, 0, absl::ZeroDuration(),
                        MakeEasingFunctionLinear(), &sink);
  EXPECT_EQ(res, SpaceSwitchOperation::AnimationStepResult::kFinished);

  op->Commit(&sink);
  ASSERT_EQ(sink.events().size(), 3u);
  EXPECT_EQ(sink.events()[0].phase, kGestureBegan);
  EXPECT_FLOAT_EQ(static_cast<float>(sink.events()[0].progress),
                  static_cast<float>(kEpsilon));
  EXPECT_EQ(sink.events()[1].phase, kGestureChanged);
  EXPECT_DOUBLE_EQ(sink.events()[1].progress, 1.0);
  EXPECT_EQ(sink.events()[2].phase, kGestureEnded);
  EXPECT_DOUBLE_EQ(sink.events()[2].progress, 1.0);
  EXPECT_DOUBLE_EQ(sink.events()[2].velocity_y, 6.0);
}

TEST(CompensatedVerticalOperationTest, NaturalScrollingSignsOnMacOS26And27) {
  const absl::FlagSaver flag_saver;

  absl::SetFlag(&FLAGS_force_os_version, 26);
  {
    CFUniquePtr<CGEventRef> ev_no_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, false);
    CFUniquePtr<CGEventRef> ev_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, true);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(ev_no_nat.get(),
                                                kCGEventGestureSwipeProgress),
                     1.0);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(ev_nat.get(), kCGEventGestureSwipeProgress),
        1.0);
  }

  absl::SetFlag(&FLAGS_force_os_version, 27);
  {
    CFUniquePtr<CGEventRef> ev_no_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, false);
    CFUniquePtr<CGEventRef> ev_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, true);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(ev_no_nat.get(),
                                                kCGEventGestureSwipeProgress),
                     1.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(ev_no_nat.get(),
                                                kCGEventGestureSwipeVelocityY),
                     6.0);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(ev_nat.get(), kCGEventGestureSwipeProgress),
        -1.0);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(ev_nat.get(), kCGEventGestureSwipeVelocityY),
        -6.0);
  }
}

}  // namespace
}  // namespace fasterswiper
