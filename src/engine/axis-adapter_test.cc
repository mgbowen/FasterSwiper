#include "src/engine/axis-adapter.h"

#include "src/cf-util.h"
#include "src/engine/const.h"
#include "src/space-state.h"

#include <absl/time/time.h>
#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

TEST(AxisAdapterTest, AppExposeHorizontalAxisAdapter_MacOS26) {
  auto display_id = WrapCFUnique(
      CFStringCreateWithCString(nullptr, "test-display", kCFStringEncodingUTF8));
  SpaceState space_state(std::move(display_id), {101, 102, 103}, /*index=*/1);

  AppExposeHorizontalAxisAdapter_MacOS26 adapter(space_state);

  EXPECT_EQ(adapter.debug_name(), "AppExposeHorizontalAxisAdapter_MacOS26");
  EXPECT_EQ(adapter.movement_direction(), Axis::kHorizontal);

  const auto [soft_min, soft_max] = adapter.position_soft_limits();
  EXPECT_EQ(soft_min, 1 * kOneSwipeInNanoswipes);
  EXPECT_EQ(soft_max, 1 * kOneSwipeInNanoswipes);

  EXPECT_TRUE(
      adapter.WaitForCommittedPositionChanged(0, absl::ZeroDuration()));

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

  EXPECT_TRUE(
      adapter.WaitForCommittedPositionChanged(0, absl::ZeroDuration()));

  EXPECT_DOUBLE_EQ(adapter.NanoswipesToProgress(0), 0.0);
  EXPECT_EQ(adapter.ProgressToNanoswipes(0.0), 0);

  const int64_t one_swipe = kOneSwipeInNanoswipes;
  const double progress_one_swipe = adapter.NanoswipesToProgress(one_swipe);
  EXPECT_EQ(adapter.ProgressToNanoswipes(progress_one_swipe), one_swipe);

  const int64_t neg_one_swipe = -1 * kOneSwipeInNanoswipes;
  const double progress_neg_swipe = adapter.NanoswipesToProgress(neg_one_swipe);
  EXPECT_EQ(adapter.ProgressToNanoswipes(progress_neg_swipe), neg_one_swipe);
}

} // namespace
} // namespace fasterswiper
