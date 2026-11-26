#include "src/engine/space-switch-operation.h"

#include "src/compatibility.h"
#include "src/easing.h"
#include "src/engine/axis-adapter.h"
#include "src/engine/const.h"
#include "src/event.h"
#include "src/fp1616.h"
#include "src/gesture-serialization.h"
#include "src/macos-private.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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
  uint64_t cg_timestamp = 0;
  std::optional<IOHIDSystemQueueElementData> hid_element;
};

class RecordingCGEventSink : public CGEventSink {
public:
  void Post(CGEventRef absl_nonnull event) override {
    std::optional<IOHIDSystemQueueElementData> hid_element;
    if (auto event_data = DeserializeCGEventData(event);
        event_data.ok() && event_data->fields.contains(4205)) {
      if (const auto* blob =
              std::get_if<std::string>(&event_data->fields.at(4205))) {
        if (auto parsed = DeserializeIOHIDSystemQueueElementData(*blob);
            parsed.ok()) {
          hid_element = std::move(*parsed);
        }
      }
    }

    events_.push_back(RecordedEvent{
        .phase = static_cast<int>(
            CGEventGetIntegerValueField(event, kCGEventGesturePhase)),
        .progress =
            CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress),
        .velocity_y =
            CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY),
        .velocity_x =
            CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX),
        .cg_timestamp = CGEventGetTimestamp(event),
        .hid_element = std::move(hid_element),
    });
  }

  const std::vector<RecordedEvent>& events() const { return events_; }

private:
  std::vector<RecordedEvent> events_;
};

class FakeAxisAdapter : public AxisAdapter {
public:
  FakeAxisAdapter(Axis axis, int64_t initial_position,
                  std::pair<int64_t, int64_t> soft_limits,
                  std::optional<int64_t>* last_wait_target = nullptr)
      : axis_(axis),
        committed_position_(initial_position),
        soft_limits_(soft_limits),
        last_wait_target_(last_wait_target) {}

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
      int64_t /*original_position*/, int64_t target_position,
      absl::Duration /*deadline*/) const override {
    if (last_wait_target_ != nullptr) {
      *last_wait_target_ = target_position;
    }
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
  std::optional<int64_t>* last_wait_target_ = nullptr;
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

  // Exit drag: MC (+1e6) -> Desktop (0) -> past 0 (-0.4e6) stays locked to
  // [0, +1e6], clamping at Desktop (0) without chaining into App Expose (-1e6).
  {
    int64_t exit_now_ns = 3'000'000'000LL;
    auto maybe_exit = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, kOneSwipeInNanoswipes,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return exit_now_ns; });
    ASSERT_TRUE(maybe_exit.ok());
    auto exit_op = *std::move(maybe_exit);
    RecordingCGEventSink exit_sink;

    exit_now_ns += 8'000'000LL;
    exit_op->SetPosition(500'000, &exit_sink);
    EXPECT_EQ(exit_op->position_soft_limits(),
              std::make_pair(int64_t{0}, kOneSwipeInNanoswipes));
    exit_now_ns += 8'000'000LL;
    exit_op->SetPosition(0, &exit_sink);
    exit_now_ns += 8'000'000LL;
    exit_op->SetPosition(-400'000, &exit_sink);
    for (const auto& ev : exit_sink.events()) {
      EXPECT_NE(ev.phase, kGestureEnded);
      EXPECT_NE(ev.phase, kGestureCancelled);
      EXPECT_LT(ev.progress, 0.0);
    }

    exit_op->Commit(&exit_sink);
    EXPECT_EQ(exit_op->position(), 0);
    AssertCompensatedSegmentInvariants(exit_sink.events(),
                                       /*expected_segments=*/1);
    EXPECT_EQ(exit_sink.events().back().phase, kGestureEnded);
    EXPECT_DOUBLE_EQ(exit_sink.events().back().progress, -1.0);
  }
}

TEST(CompensatedVerticalOperationTest,
     DirectionLocksToInitialSegmentMatchingStockMacOS) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  const EasingFunction linear = MakeEasingFunctionLinear();
  constexpr int64_t kDtNs = 1'000'000'000LL / 240;

  // Case 1: Desktop (0) -> drag UP (+0.3e6) -> drag DOWN past 0 (-0.5e6) ->
  // release targeting App Expose (-1e6). Must stay in the single upward
  // segment, never emit negative progress, lock soft limits to [0, +1e6], and
  // cancel cleanly back to Desktop (0).
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
    EXPECT_EQ(op->position_soft_limits(),
              std::make_pair(int64_t{0}, kOneSwipeInNanoswipes));

    fake_now_ns += 8'000'000LL;
    op->SetPosition(-500'000, &sink);
    EXPECT_GE(op->position(), 0);

    // Even if a caller passes -1e6 as the release target, StepAnimation must
    // clamp to origin (0) and cancel cleanly without entering App Expose.
    const int64_t release_start = op->position();
    int64_t elapsed_ns = 0;
    bool finished = false;
    for (int tick = 0; tick <= 120; ++tick) {
      const auto res =
          op->StepAnimation(release_start, -kOneSwipeInNanoswipes, elapsed_ns,
                            absl::Milliseconds(100), linear, &sink);
      if (res == SpaceSwitchOperation::AnimationStepResult::kFinished) {
        finished = true;
        break;
      }
      fake_now_ns += kDtNs;
      elapsed_ns += kDtNs;
    }
    EXPECT_TRUE(finished);

    op->Commit(&sink);
    EXPECT_EQ(op->position(), 0);
    AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
    EXPECT_EQ(sink.events().back().phase, kGestureCancelled);
    for (const auto& ev : sink.events()) {
      EXPECT_GT(ev.progress, 0.0);
    }
  }

  // Case 2: Desktop (0) -> drag DOWN (-0.3e6) -> drag UP past 0 (+0.5e6) ->
  // release targeting Mission Control (+1e6). Must stay in the single downward
  // segment, never emit positive progress, lock soft limits to [-1e6, 0], and
  // cancel cleanly back to Desktop (0).
  {
    int64_t fake_now_ns = 2'000'000'000LL;
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
    op->SetPosition(-300'000, &sink);
    EXPECT_EQ(op->position_soft_limits(),
              std::make_pair(-kOneSwipeInNanoswipes, int64_t{0}));

    fake_now_ns += 8'000'000LL;
    op->SetPosition(500'000, &sink);
    EXPECT_LE(op->position(), 0);

    const int64_t release_start = op->position();
    int64_t elapsed_ns = 0;
    bool finished = false;
    for (int tick = 0; tick <= 120; ++tick) {
      const auto res =
          op->StepAnimation(release_start, kOneSwipeInNanoswipes, elapsed_ns,
                            absl::Milliseconds(100), linear, &sink);
      if (res == SpaceSwitchOperation::AnimationStepResult::kFinished) {
        finished = true;
        break;
      }
      fake_now_ns += kDtNs;
      elapsed_ns += kDtNs;
    }
    EXPECT_TRUE(finished);

    op->Commit(&sink);
    EXPECT_EQ(op->position(), 0);
    AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
    EXPECT_EQ(sink.events().back().phase, kGestureCancelled);
    for (const auto& ev : sink.events()) {
      EXPECT_LT(ev.progress, 0.0);
    }
  }

  // Case 3: App Expose (-1e6) -> drag UP past Desktop (0) toward Mission
  // Control (+0.5e6) -> release targeting +1e6. Must clamp at Desktop (0) in a
  // single segment and never chain into Mission Control.
  {
    int64_t fake_now_ns = 3'000'000'000LL;
    auto maybe_op = CompensatedVerticalSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(
            Axis::kVertical, -kOneSwipeInNanoswipes,
            std::make_pair(-kOneSwipeInNanoswipes, kOneSwipeInNanoswipes)),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false},
        [&] { return fake_now_ns; });
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    fake_now_ns += 8'000'000LL;
    op->SetPosition(-400'000, &sink);
    EXPECT_EQ(op->position_soft_limits(),
              std::make_pair(-kOneSwipeInNanoswipes, int64_t{0}));

    fake_now_ns += 8'000'000LL;
    op->SetPosition(500'000, &sink);
    EXPECT_LE(op->position(), 0);

    const int64_t release_start = op->position();
    int64_t elapsed_ns = 0;
    bool finished = false;
    for (int tick = 0; tick <= 120; ++tick) {
      const auto res =
          op->StepAnimation(release_start, kOneSwipeInNanoswipes, elapsed_ns,
                            absl::Milliseconds(100), linear, &sink);
      if (res == SpaceSwitchOperation::AnimationStepResult::kFinished) {
        finished = true;
        break;
      }
      fake_now_ns += kDtNs;
      elapsed_ns += kDtNs;
    }
    EXPECT_TRUE(finished);

    op->Commit(&sink);
    EXPECT_EQ(op->position(), 0);
    AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/1);
    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_DOUBLE_EQ(sink.events().back().progress, 1.0);
  }

  // Case 4: Mission Control (+1e6) -> overdrag UP rubberband (+1.2e6) -> pull
  // back DOWN past +1e6 into Desktop exit (+0.5e6) -> drag past 0 (-0.4e6).
  // Rubberband ends cleanly at +1e6 and transitions into the locked [0, +1e6]
  // exit segment (never chaining into App Expose).
  {
    int64_t fake_now_ns = 4'000'000'000LL;
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
    op->SetPosition(1'200'000, &sink);
    EXPECT_EQ(op->position(), 1'200'000);

    fake_now_ns += 8'000'000LL;
    op->SetPosition(500'000, &sink);
    EXPECT_EQ(op->position_soft_limits(),
              std::make_pair(int64_t{0}, kOneSwipeInNanoswipes));

    fake_now_ns += 8'000'000LL;
    op->SetPosition(-400'000, &sink);
    EXPECT_GE(op->position(), 0);

    op->Commit(&sink);
    EXPECT_EQ(op->position(), 0);
    // Segment 1: cancelled rubberband; Segment 2: locked MC -> Desktop exit.
    AssertCompensatedSegmentInvariants(sink.events(), /*expected_segments=*/2);
    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_DOUBLE_EQ(sink.events().back().progress, -1.0);
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

void AssertStrictlyMonotonicTimestamps(
    const std::vector<RecordedEvent>& events) {
  uint64_t prev_cg_ts = 0;
  uint64_t prev_hid_ts = 0;
  for (std::size_t i = 0; i < events.size(); ++i) {
    EXPECT_GT(events[i].cg_timestamp, prev_cg_ts) << "event index " << i;
    prev_cg_ts = events[i].cg_timestamp;
    ASSERT_TRUE(events[i].hid_element.has_value()) << "event index " << i;
    EXPECT_GT(events[i].hid_element->header.timestamp, prev_hid_ts)
        << "event index " << i;
    prev_hid_ts = events[i].hid_element->header.timestamp;
  }
}

TEST(ContinuousSpaceSwitchOperationTest,
     MultiSpaceAnimatedJumpBothDirectionsNaturalScrollingAndMacOS26And27) {
  const absl::FlagSaver flag_saver;

  constexpr int64_t kSpaceCount = 5;
  const auto soft_limits =
      std::make_pair(int64_t{0}, (kSpaceCount - 1) * kOneSwipeInNanoswipes);

  struct Case {
    int64_t origin;
    int64_t target;
    int64_t dir;
    int64_t num_spaces;
    bool natural_scrolling;
  };
  const std::vector<Case> cases = {
      {0, 3 * kOneSwipeInNanoswipes, 1, 3, false},
      {0, 3 * kOneSwipeInNanoswipes, 1, 3, true},
      {3 * kOneSwipeInNanoswipes, 0, -1, 3, false},
      {3 * kOneSwipeInNanoswipes, 0, -1, 3, true},
  };

  for (int os_version : {26, 27}) {
    absl::SetFlag(&FLAGS_force_os_version, os_version);

    for (const Case& c : cases) {
      SCOPED_TRACE(::testing::Message()
                   << "os_version=" << os_version << " origin=" << c.origin
                   << " target=" << c.target
                   << " natural_scrolling=" << c.natural_scrolling);

      std::optional<int64_t> wait_target;
      auto maybe_op = ContinuousSpaceSwitchOperation::Create(
          std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, c.origin,
                                            soft_limits, &wait_target),
          SpaceSwitchOperation::Options{.natural_scrolling_enabled =
                                            c.natural_scrolling});
      ASSERT_TRUE(maybe_op.ok()) << maybe_op.status();
      auto op = *std::move(maybe_op);
      RecordingCGEventSink sink;

      // Simulate a normal animated switch whose penultimate position is within
      // 0.05 spaces of target.
      op->SetPosition(c.origin + c.dir * 1'500'000, &sink);
      op->SetPosition(c.target - c.dir * 10'000, &sink);
      op->SetPosition(c.target, &sink);

      const std::size_t commit_start = sink.events().size();
      op->Commit(&sink);

      EXPECT_EQ(wait_target, c.target);
      AssertStrictlyMonotonicTimestamps(sink.events());

      // Commit burst for N spaces: (N-1) * (Ended + Began) + 1 final Ended.
      const std::size_t expected_commit_events =
          static_cast<std::size_t>(2 * c.num_spaces - 1);
      ASSERT_EQ(sink.events().size() - commit_start, expected_commit_events);

      // Only macOS 27 negates wire progress/velocity for natural scrolling.
      const double wire_sign =
          (os_version >= 27 && c.natural_scrolling) ? -1.0 : 1.0;
      const double signed_dir = wire_sign * static_cast<double>(c.dir);

      for (int64_t i = 0; i < c.num_spaces - 1; ++i) {
        const auto& ended =
            sink.events()[commit_start + static_cast<std::size_t>(2 * i)];
        const auto& began =
            sink.events()[commit_start + static_cast<std::size_t>(2 * i + 1)];

        EXPECT_EQ(ended.phase, kGestureEnded);
        EXPECT_FLOAT_EQ(static_cast<float>(ended.progress),
                        static_cast<float>(signed_dir * kEpsilon));
        EXPECT_DOUBLE_EQ(ended.velocity_x, 0.0);
        ASSERT_TRUE(ended.hid_element.has_value());
        ASSERT_EQ(ended.hid_element->events.size(), 2u);
        ASSERT_TRUE(std::holds_alternative<IOHIDVelocityEventData>(
            ended.hid_element->events[1]));
        const auto& vel =
            std::get<IOHIDVelocityEventData>(ended.hid_element->events[1]);
        EXPECT_EQ(vel.base.type, IOHIDEventType::kIOHIDEventTypeVelocity);
        EXPECT_EQ(vel.velocity_x, 0);

        EXPECT_EQ(began.phase, kGestureBegan);
        EXPECT_FLOAT_EQ(static_cast<float>(began.progress),
                        static_cast<float>(-signed_dir * kEpsilon));
        ASSERT_TRUE(began.hid_element.has_value());
        EXPECT_EQ(began.hid_element->events.size(), 1u);
      }

      const auto& final_ended = sink.events().back();
      EXPECT_EQ(final_ended.phase, kGestureEnded);
      EXPECT_FLOAT_EQ(static_cast<float>(final_ended.progress),
                      static_cast<float>(signed_dir * kEpsilon));
      EXPECT_DOUBLE_EQ(final_ended.velocity_x,
                       signed_dir * 8.0 * static_cast<double>(kSpaceCount));
    }
  }
}

TEST(ContinuousSpaceSwitchOperationTest,
     ZeroDurationAndLargeGapSelectsClampedInstantVelocity) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  for (int64_t space_count : {2, 4, 9, 21, 22, 24}) {
    SCOPED_TRACE(::testing::Message() << "space_count=" << space_count);
    const auto soft_limits =
        std::make_pair(int64_t{0}, (space_count - 1) * kOneSwipeInNanoswipes);
    const int64_t target = (space_count - 1) * kOneSwipeInNanoswipes;

    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok()) << maybe_op.status();
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    const auto res = op->StepAnimation(0, target, 0, absl::ZeroDuration(),
                                       MakeEasingFunctionLinear(), &sink);
    EXPECT_EQ(res, SpaceSwitchOperation::AnimationStepResult::kFinished);
    op->Commit(&sink);

    const double expected_v =
        std::min(1500.0 * static_cast<double>(space_count), 32000.0);
    const auto& final_ended = sink.events().back();
    EXPECT_EQ(final_ended.phase, kGestureEnded);
    EXPECT_FLOAT_EQ(static_cast<float>(final_ended.progress),
                    static_cast<float>(kEpsilon));
    EXPECT_DOUBLE_EQ(final_ended.velocity_x, expected_v);
    ASSERT_TRUE(final_ended.hid_element.has_value());
    ASSERT_EQ(final_ended.hid_element->events.size(), 2u);
    const auto& vel =
        std::get<IOHIDVelocityEventData>(final_ended.hid_element->events[1]);
    EXPECT_EQ(vel.velocity_x, DoubleToFixedFP1616(expected_v));
  }

  // Also verify 2-tick large-gap animation (penultimate gap > 0.05 spaces)
  // selects the instant velocity.
  {
    constexpr int64_t kSpaceCount = 6;
    const auto soft_limits =
        std::make_pair(int64_t{0}, (kSpaceCount - 1) * kOneSwipeInNanoswipes);
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(1'000'000, &sink);
    op->SetPosition(2'000'000, &sink);
    op->Commit(&sink);

    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_DOUBLE_EQ(sink.events().back().velocity_x, 1500.0 * kSpaceCount);
  }
}

TEST(ContinuousSpaceSwitchOperationTest,
     OvershootAndReturnHoldsBackStrictlyOnOriginSide) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  const auto soft_limits =
      std::make_pair(int64_t{0}, 3 * kOneSwipeInNanoswipes);

  // Positive travel (0 -> overshoot 2.3e6 -> return to 2.0e6):
  // pre-commit Changed must be strictly < 2.0 (origin side), not > 2.0.
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(2'300'000, &sink);
    op->SetPosition(2'010'000, &sink);
    op->SetPosition(2'000'000, &sink);

    const double expected_origin_side_progress =
        static_cast<double>(
            2'000'000 -
            ContinuousSpaceSwitchOperation::kBoundaryHoldbackNanoswipes) /
        kOneSwipeInNanoswipes;
    ASSERT_FALSE(sink.events().empty());
    EXPECT_EQ(sink.events().back().phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(expected_origin_side_progress));
    EXPECT_LT(sink.events().back().progress, 2.0);

    op->Commit(&sink);
    // Direction must be +1 (origin -> target), NOT -1 (latest_direction_).
    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(kEpsilon));
    EXPECT_DOUBLE_EQ(sink.events().back().velocity_x, 8.0 * 4);
  }

  // Negative travel (2.0e6 -> overshoot -0.3e6 -> return to 0):
  // pre-commit Changed must be strictly on origin side (> -2.0 progress from
  // origin, i.e., position = +20).
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 2'000'000,
                                          soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(-300'000, &sink);
    op->SetPosition(-10'000, &sink);
    op->SetPosition(0, &sink);

    const double expected_origin_side_progress =
        static_cast<double>(
            ContinuousSpaceSwitchOperation::kBoundaryHoldbackNanoswipes -
            2'000'000) /
        kOneSwipeInNanoswipes;
    ASSERT_FALSE(sink.events().empty());
    EXPECT_EQ(sink.events().back().phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(expected_origin_side_progress));
    EXPECT_GT(sink.events().back().progress, -2.0);

    op->Commit(&sink);
    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(-kEpsilon));
    EXPECT_DOUBLE_EQ(sink.events().back().velocity_x, -8.0 * 4);
  }

  // Mid-drag commit while strictly past target (0 -> 2.3e6 -> Commit):
  // CommitLocked must emit an origin-side corrective kGestureChanged before
  // the first kGestureEnded, and use final velocity 0.0 so a DATA_REPLACE
  // overwrite cannot catapult off the far side.
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(2'300'000, &sink);
    const std::size_t commit_start = sink.events().size();
    op->Commit(&sink);

    ASSERT_GT(sink.events().size(), commit_start);
    EXPECT_EQ(sink.events()[commit_start].phase, kGestureChanged);
    EXPECT_FLOAT_EQ(
        static_cast<float>(sink.events()[commit_start].progress),
        static_cast<float>(
            static_cast<double>(
                2'000'000 -
                ContinuousSpaceSwitchOperation::kBoundaryHoldbackNanoswipes) /
            kOneSwipeInNanoswipes));
    EXPECT_EQ(sink.events()[commit_start + 1].phase, kGestureEnded);
    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_DOUBLE_EQ(sink.events().back().velocity_x, 0.0);
    ASSERT_TRUE(sink.events().back().hid_element.has_value());
    ASSERT_EQ(sink.events().back().hid_element->events.size(), 2u);
    EXPECT_EQ(std::get<IOHIDVelocityEventData>(
                  sink.events().back().hid_element->events[1])
                  .velocity_x,
              0);
  }

  // Far-side large-gap / duration=0 return (0 -> 2.3e6 -> 2.0e6 -> Commit):
  // penultimate position (2.3e6) is on the far side with gap > 0.05 spaces and
  // N = 2 > 1, so final velocity must be 0.0 (not +1500 * M).
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(2'300'000, &sink);
    op->SetPosition(2'000'000, &sink);
    op->Commit(&sink);

    EXPECT_EQ(sink.events().back().phase, kGestureEnded);
    EXPECT_FLOAT_EQ(static_cast<float>(sink.events().back().progress),
                    static_cast<float>(kEpsilon));
    EXPECT_DOUBLE_EQ(sink.events().back().velocity_x, 0.0);
  }
}

TEST(ContinuousSpaceSwitchOperationTest, NoOpAndSubOneSpaceCommitsAreGuarded) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  const auto soft_limits =
      std::make_pair(int64_t{0}, 3 * kOneSwipeInNanoswipes);

  // Never moved -> Commit emits zero events (no stray kGestureCancelled).
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(0, &sink);
    op->Commit(&sink);
    EXPECT_TRUE(sink.events().empty());
  }

  // Sub-1-space displacement -> Commit emits kGestureCancelled, never Ended.
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(400'000, &sink);
    op->Commit(&sink);
    ASSERT_FALSE(sink.events().empty());
    EXPECT_EQ(sink.events().back().phase, kGestureCancelled);
    for (const auto& ev : sink.events()) {
      EXPECT_NE(ev.phase, kGestureEnded);
    }
  }

  // Moved out and returned to exact origin -> kGestureCancelled, never Ended.
  {
    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok());
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    op->SetPosition(400'000, &sink);
    op->SetPosition(0, &sink);
    EXPECT_EQ(op->position(), 0);
    // Returning to origin must nudge away from 0.0 on the approach side so
    // kGestureChanged never sends progress == 0.0.
    ASSERT_FALSE(sink.events().empty());
    EXPECT_EQ(sink.events().back().phase, kGestureChanged);
    EXPECT_FLOAT_EQ(
        static_cast<float>(sink.events().back().progress),
        static_cast<float>(
            static_cast<double>(
                ContinuousSpaceSwitchOperation::kBoundaryHoldbackNanoswipes) /
            kOneSwipeInNanoswipes));

    op->Commit(&sink);
    ASSERT_FALSE(sink.events().empty());
    EXPECT_EQ(sink.events().back().phase, kGestureCancelled);
    for (const auto& ev : sink.events()) {
      EXPECT_NE(ev.phase, kGestureEnded);
    }
  }
}

TEST(ContinuousSpaceSwitchOperationTest,
     SixtyAnd120HzLargePenultimateGapHoldsOneExtraVsyncForSmoothCommit) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  constexpr int64_t k60HzDtNs = 16'666'667LL;
  constexpr int64_t k120HzDtNs = 8'333'333LL;
  const EasingFunction linear = MakeEasingFunctionLinear();
  const EasingFunction quad_out = MakeEasingFunctionEaseOutQuadratic();
  const EasingFunction quad_in = [](double t) { return t * t; };

  struct Case {
    const char* name;
    int64_t space_count;
    int64_t target_spaces;
    absl::Duration duration;
    int64_t dt_ns;
    EasingFunction easing;
  };
  const std::vector<Case> cases = {
      {"60Hz_200ms_Linear_1space", 4, 1, absl::Milliseconds(200), k60HzDtNs,
       linear},
      {"60Hz_200ms_EaseIn_1space", 4, 1, absl::Milliseconds(200), k60HzDtNs,
       quad_in},
      {"60Hz_200ms_QuadOut_8space", 10, 8, absl::Milliseconds(200), k60HzDtNs,
       quad_out},
      {"60Hz_100ms_QuadOut_2space", 5, 2, absl::Milliseconds(100), k60HzDtNs,
       quad_out},
      {"60Hz_50ms_QuadOut_1space", 4, 1, absl::Milliseconds(50), k60HzDtNs,
       quad_out},
      {"60Hz_10ms_ShortSingleTick_1space", 4, 1, absl::Milliseconds(10),
       k60HzDtNs, linear},
      {"120Hz_200ms_Linear_2space", 5, 2, absl::Milliseconds(200), k120HzDtNs,
       linear},
  };

  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    const auto soft_limits =
        std::make_pair(int64_t{0}, (c.space_count - 1) * kOneSwipeInNanoswipes);
    const int64_t target = c.target_spaces * kOneSwipeInNanoswipes;

    auto maybe_op = ContinuousSpaceSwitchOperation::Create(
        std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
        SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
    ASSERT_TRUE(maybe_op.ok()) << maybe_op.status();
    auto op = *std::move(maybe_op);
    RecordingCGEventSink sink;

    bool reached_target = false;
    int ticks_at_target = 0;
    bool finished = false;
    for (int tick = 1; tick <= 60; ++tick) {
      const int64_t elapsed_ns = tick * c.dt_ns;
      const auto res =
          op->StepAnimation(0, target, elapsed_ns, c.duration, c.easing, &sink);
      if (op->position() == target) {
        reached_target = true;
        ++ticks_at_target;
      }
      if (res == SpaceSwitchOperation::AnimationStepResult::kFinished) {
        finished = true;
        break;
      }
    }

    EXPECT_TRUE(reached_target);
    EXPECT_TRUE(finished);
    // Exactly 1 extra VSync tick held at target_position before finishing.
    EXPECT_EQ(ticks_at_target, 2);

    // The two final kGestureChanged events before Commit must both hold at
    // target - holdback.
    ASSERT_GE(sink.events().size(), 3u);
    const double expected_hold_progress =
        static_cast<double>(
            target -
            ContinuousSpaceSwitchOperation::kBoundaryHoldbackNanoswipes) /
        kOneSwipeInNanoswipes;
    const auto& hold_first = sink.events()[sink.events().size() - 2];
    const auto& hold_second = sink.events().back();
    EXPECT_EQ(hold_first.phase, kGestureChanged);
    EXPECT_EQ(hold_second.phase, kGestureChanged);
    EXPECT_FLOAT_EQ(static_cast<float>(hold_first.progress),
                    static_cast<float>(expected_hold_progress));
    EXPECT_FLOAT_EQ(static_cast<float>(hold_second.progress),
                    static_cast<float>(expected_hold_progress));

    op->Commit(&sink);
    const auto& final_ended = sink.events().back();
    EXPECT_EQ(final_ended.phase, kGestureEnded);
    EXPECT_FLOAT_EQ(static_cast<float>(final_ended.progress),
                    static_cast<float>(kEpsilon));
    EXPECT_DOUBLE_EQ(final_ended.velocity_x,
                     8.0 * static_cast<double>(c.space_count));
  }
}

TEST(ContinuousSpaceSwitchOperationTest,
     SetPositionDuringTargetHoldClearsPendingHold) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  const auto soft_limits =
      std::make_pair(int64_t{0}, 3 * kOneSwipeInNanoswipes);
  auto maybe_op = ContinuousSpaceSwitchOperation::Create(
      std::make_unique<FakeAxisAdapter>(Axis::kHorizontal, 0, soft_limits),
      SpaceSwitchOperation::Options{.natural_scrolling_enabled = false});
  ASSERT_TRUE(maybe_op.ok());
  auto op = *std::move(maybe_op);
  RecordingCGEventSink sink;

  const EasingFunction linear = MakeEasingFunctionLinear();
  const absl::Duration duration = absl::Milliseconds(100);

  // Reach target with a large penultimate gap -> holds for 1 extra tick.
  EXPECT_EQ(op->StepAnimation(0, kOneSwipeInNanoswipes, 50'000'000LL, duration,
                              linear, &sink),
            SpaceSwitchOperation::AnimationStepResult::kContinue);
  EXPECT_EQ(op->StepAnimation(0, kOneSwipeInNanoswipes, 100'000'000LL, duration,
                              linear, &sink),
            SpaceSwitchOperation::AnimationStepResult::kContinue);
  EXPECT_EQ(op->position(), kOneSwipeInNanoswipes);

  // Interrupt during the hold with a manual drag.
  op->SetPosition(1'400'000, &sink);
  EXPECT_EQ(op->position(), 1'400'000);

  // Subsequent animation to 2 spaces must animate normally and hold once at
  // 2e6.
  EXPECT_EQ(op->StepAnimation(1'400'000, 2 * kOneSwipeInNanoswipes,
                              100'000'000LL, duration, linear, &sink),
            SpaceSwitchOperation::AnimationStepResult::kContinue);
  EXPECT_EQ(op->position(), 2 * kOneSwipeInNanoswipes);
  EXPECT_EQ(op->StepAnimation(1'400'000, 2 * kOneSwipeInNanoswipes,
                              116'666'667LL, duration, linear, &sink),
            SpaceSwitchOperation::AnimationStepResult::kFinished);

  op->Commit(&sink);
  EXPECT_EQ(sink.events().back().phase, kGestureEnded);
  EXPECT_DOUBLE_EQ(sink.events().back().velocity_x, 8.0 * 4);
}

}  // namespace
}  // namespace fasterswiper
