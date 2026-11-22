#include "src/engine/axis-adapter.h"

#include "src/cf-util.h"
#include "src/engine/const.h"
#include "src/space-state.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/time/time.h>
#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

TEST(AxisAdapterTest, AppExposeHorizontalAxisAdapter_MacOS26) {
  auto display_id = WrapCFUnique(CFStringCreateWithCString(
      nullptr, "test-display", kCFStringEncodingUTF8));
  SpaceState space_state(std::move(display_id),
                         {Space{.id = 101}, Space{.id = 102}, Space{.id = 103}},
                         /*index=*/1);

  AppExposeHorizontalAxisAdapter_MacOS26 adapter(space_state);

  EXPECT_EQ(adapter.debug_name(), "AppExposeHorizontalAxisAdapter_MacOS26");
  EXPECT_EQ(adapter.movement_direction(), Axis::kHorizontal);

  const auto [soft_min, soft_max] = adapter.position_soft_limits();
  EXPECT_EQ(soft_min, 1 * kOneSwipeInNanoswipes);
  EXPECT_EQ(soft_max, 1 * kOneSwipeInNanoswipes);

  EXPECT_TRUE(adapter.WaitForCommittedPositionChanged(0, kOneSwipeInNanoswipes,
                                                      absl::ZeroDuration()));

  const int64_t nanoswipes = 500'000;
  EXPECT_DOUBLE_EQ(adapter.NanoswipesToProgress(nanoswipes),
                   space_state.SwipesToProgress(nanoswipes));
  const double progress = 0.75;
  EXPECT_EQ(adapter.ProgressToNanoswipes(progress),
            space_state.ProgressToSwipes(progress));
}

TEST(AxisAdapterTest, AppExposeHorizontalAxisAdapter_MacOS27) {
  AppExposeHorizontalAxisAdapter adapter;

  EXPECT_EQ(adapter.debug_name(), "AppExposeHorizontalAxisAdapter");
  EXPECT_EQ(adapter.movement_direction(), Axis::kHorizontal);

  auto committed = adapter.committed_position();
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(*committed, 0);

  const auto [soft_min, soft_max] = adapter.position_soft_limits();
  EXPECT_EQ(soft_min, -1 * kOneSwipeInNanoswipes);
  EXPECT_EQ(soft_max, 1 * kOneSwipeInNanoswipes);

  EXPECT_TRUE(adapter.WaitForCommittedPositionChanged(0, kOneSwipeInNanoswipes,
                                                      absl::ZeroDuration()));

  EXPECT_DOUBLE_EQ(adapter.NanoswipesToProgress(0), 0.0);
  EXPECT_EQ(adapter.ProgressToNanoswipes(0.0), 0);

  const int64_t one_swipe = kOneSwipeInNanoswipes;
  const double progress_one_swipe = adapter.NanoswipesToProgress(one_swipe);
  EXPECT_EQ(adapter.ProgressToNanoswipes(progress_one_swipe), one_swipe);

  const int64_t neg_one_swipe = -1 * kOneSwipeInNanoswipes;
  const double progress_neg_swipe = adapter.NanoswipesToProgress(neg_one_swipe);
  EXPECT_EQ(adapter.ProgressToNanoswipes(progress_neg_swipe), neg_one_swipe);
}

TEST(AxisAdapterTest, VerticalAxisAdapter_DesktopSpace) {
  auto display_id = WrapCFUnique(CFStringCreateWithCString(
      nullptr, "test-display", kCFStringEncodingUTF8));
  SpaceState space_state(std::move(display_id),
                         {Space{.id = 101, .is_desktop = true}},
                         /*index=*/0);

  VerticalAxisAdapter adapter(space_state);

  EXPECT_EQ(adapter.debug_name(), "VerticalAxisAdapter");
  EXPECT_EQ(adapter.movement_direction(), Axis::kVertical);

  auto committed = adapter.committed_position();
  ASSERT_TRUE(committed.ok());
  EXPECT_EQ(*committed, 0);

  const auto [soft_min, soft_max] = adapter.position_soft_limits();
  EXPECT_EQ(soft_min, -1 * kOneSwipeInNanoswipes);
  EXPECT_EQ(soft_max, 1 * kOneSwipeInNanoswipes);

  EXPECT_DOUBLE_EQ(adapter.NanoswipesToProgress(kOneSwipeInNanoswipes), 1.0);
  EXPECT_EQ(adapter.ProgressToNanoswipes(1.0), kOneSwipeInNanoswipes);
}

TEST(AxisAdapterTest, VerticalAxisAdapter_NonDesktopSpace) {
  auto display_id = WrapCFUnique(CFStringCreateWithCString(
      nullptr, "test-display", kCFStringEncodingUTF8));
  SpaceState space_state(std::move(display_id),
                         {Space{.id = 101, .is_desktop = false}},
                         /*index=*/0);

  VerticalAxisAdapter adapter(space_state);

  const auto [soft_min, soft_max] = adapter.position_soft_limits();
  EXPECT_EQ(soft_min, 0);
  EXPECT_EQ(soft_max, 1 * kOneSwipeInNanoswipes);
}

TEST(AxisAdapterTest, SingleSpaceStateUnitFactorIsFinite) {
  auto display_id = WrapCFUnique(CFStringCreateWithCString(
      nullptr, "test-display", kCFStringEncodingUTF8));
  SpaceState space_state(std::move(display_id),
                         {Space{.id = 101, .is_desktop = true}},
                         /*index=*/0);

  EXPECT_EQ(space_state.count(), 1);
  EXPECT_TRUE(
      std::isfinite(space_state.SwipesToProgress(kOneSwipeInNanoswipes)));
  EXPECT_DOUBLE_EQ(space_state.SwipesToProgress(kOneSwipeInNanoswipes), 1.0);
  EXPECT_EQ(space_state.ProgressToSwipes(1.0), kOneSwipeInNanoswipes);

  HorizontalAxisAdapter adapter(space_state);
  const auto [soft_min, soft_max] = adapter.position_soft_limits();
  EXPECT_EQ(soft_min, 0);
  EXPECT_EQ(soft_max, 0);
}

class SequenceCommittedPositionAdapter : public AxisAdapter {
public:
  explicit SequenceCommittedPositionAdapter(
      std::vector<absl::StatusOr<int64_t>> responses)
      : responses_(std::move(responses)) {}

  constexpr absl::string_view debug_name() const override {
    return "SequenceCommittedPositionAdapter";
  }

  Axis movement_direction() const override { return Axis::kHorizontal; }

  double NanoswipesToProgress(int64_t nanoswipes) const override {
    return static_cast<double>(nanoswipes) / kOneSwipeInNanoswipes;
  }

  int64_t ProgressToNanoswipes(double progress) const override {
    return static_cast<int64_t>(progress * kOneSwipeInNanoswipes);
  }

  absl::StatusOr<int64_t> committed_position() const override {
    const size_t idx = call_count_.fetch_add(1, std::memory_order_relaxed);
    return responses_[std::min(idx, responses_.size() - 1)];
  }

  std::pair<int64_t, int64_t> position_soft_limits() const override {
    return {0, 3 * kOneSwipeInNanoswipes};
  }

  size_t call_count() const {
    return call_count_.load(std::memory_order_relaxed);
  }

private:
  std::vector<absl::StatusOr<int64_t>> responses_;
  mutable std::atomic<size_t> call_count_{0};
};

TEST(AxisAdapterTest, WaitForCommittedPositionHandlesErrorsAndWaitsForTarget) {
  // Returns error, then origin, then intermediate space 1, then target space 2.
  SequenceCommittedPositionAdapter adapter({
      absl::InternalError("transient read error"),
      0,
      1 * kOneSwipeInNanoswipes,
      2 * kOneSwipeInNanoswipes,
  });

  EXPECT_TRUE(adapter.WaitForCommittedPositionChanged(
      /*original_position=*/0, /*target_position=*/2 * kOneSwipeInNanoswipes,
      absl::Milliseconds(100)));
  EXPECT_EQ(adapter.call_count(), 4u);
}

TEST(AxisAdapterTest, WaitForCommittedPositionTimesOutOnErrorsOrWrongTarget) {
  SequenceCommittedPositionAdapter error_adapter({
      absl::InternalError("persistent error"),
  });
  EXPECT_FALSE(error_adapter.WaitForCommittedPositionChanged(
      /*original_position=*/0, /*target_position=*/2 * kOneSwipeInNanoswipes,
      absl::Milliseconds(5)));
  EXPECT_GT(error_adapter.call_count(), 0u);

  SequenceCommittedPositionAdapter stuck_adapter({
      1 * kOneSwipeInNanoswipes,
  });
  EXPECT_FALSE(stuck_adapter.WaitForCommittedPositionChanged(
      /*original_position=*/0, /*target_position=*/2 * kOneSwipeInNanoswipes,
      absl::Milliseconds(5)));
}

}  // namespace
}  // namespace fasterswiper
