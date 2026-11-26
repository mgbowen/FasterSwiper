#include "src/swift/display-link-timer.h"

#include "src/animation-timer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <absl/base/nullability.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/synchronization/notification.h>
#include <absl/time/time.h>
#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

TEST(DisplayLinkTimerTest,
     CreateOffMainThreadReturnsFailedPreconditionWithoutConsumingParameters) {
  absl::StatusOr<absl_nonnull std::unique_ptr<AnimationTimer>> maybe_timer;
  bool callbacks_preserved = false;

  std::thread worker([&] {
    AnimationTimerParameters params{
        .duration = absl::Milliseconds(50),
        .tick_callback = [](int64_t) { return AnimationTickResult::kFinish; },
        .stopped_callback = [](AnimationStopReason) {},
    };
    maybe_timer = CreateDisplayLinkTimer(params);
    callbacks_preserved =
        (params.tick_callback != nullptr && params.stopped_callback != nullptr);
  });
  worker.join();

  EXPECT_FALSE(maybe_timer.ok());
  EXPECT_EQ(maybe_timer.status().code(), absl::StatusCode::kFailedPrecondition)
      << maybe_timer.status();
  EXPECT_TRUE(callbacks_preserved);
}

TEST(
    DisplayLinkTimerTest,
    CreateWithNullTickCallbackReturnsInvalidArgumentWithoutConsumingParameters) {
  AnimationTimerParameters params{
      .duration = absl::Milliseconds(50),
      .tick_callback = nullptr,
      .stopped_callback = [](AnimationStopReason) {},
  };

  auto maybe_timer = CreateDisplayLinkTimer(params);
  EXPECT_FALSE(maybe_timer.ok());
  EXPECT_EQ(maybe_timer.status().code(), absl::StatusCode::kInvalidArgument)
      << maybe_timer.status();
  EXPECT_TRUE(params.stopped_callback != nullptr);
}

TEST(DisplayLinkTimerTest, FactoryFallsBackToPeriodicTimerOffMainThread) {
  std::atomic<int> ticks{0};
  absl::Notification done;
  AnimationStopReason reason = AnimationStopReason::kCancelled;

  std::thread worker([&] {
    AnimationTimerFactory factory = MakeDisplayLinkTimerFactory();
    auto timer = factory(AnimationTimerParameters{
        .duration = absl::Milliseconds(50),
        .fallback_ticks_per_second = 240,
        .fire_immediately = true,
        .tick_callback =
            [&](int64_t) {
              return ticks.fetch_add(1) + 1 >= 3
                         ? AnimationTickResult::kFinish
                         : AnimationTickResult::kContinue;
            },
        .stopped_callback =
            [&](AnimationStopReason stop_reason) {
              reason = stop_reason;
              done.Notify();
            },
    });
    ASSERT_NE(timer, nullptr);
    ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(2)));
  });
  worker.join();

  EXPECT_GE(ticks.load(), 3);
  EXPECT_EQ(reason, AnimationStopReason::kFinished);
}

TEST(DisplayLinkTimerTest, ZeroDurationFiresImmediatelyAndCompletes) {
  AnimationTimerFactory factory = MakeDisplayLinkTimerFactory();
  std::atomic<int> ticks{0};
  std::atomic<int64_t> first_elapsed{-1};
  absl::Notification done;
  AnimationStopReason reason = AnimationStopReason::kCancelled;

  auto timer = factory(AnimationTimerParameters{
      .duration = absl::ZeroDuration(),
      .tick_callback =
          [&](int64_t elapsed_ns) {
            if (ticks.fetch_add(1) == 0) {
              first_elapsed.store(elapsed_ns);
            }
            return AnimationTickResult::kFinish;
          },
      .stopped_callback =
          [&](AnimationStopReason stop_reason) {
            reason = stop_reason;
            done.Notify();
          },
  });
  ASSERT_NE(timer, nullptr);
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(2)));
  EXPECT_EQ(ticks.load(), 1);
  EXPECT_GE(first_elapsed.load(), 0);
  EXPECT_EQ(reason, AnimationStopReason::kFinished);
}

TEST(DisplayLinkTimerTest, RapidCreateAndCancelStressTest) {
  AnimationTimerFactory factory = MakeDisplayLinkTimerFactory();

  for (int i = 0; i < 50; ++i) {
    std::atomic<bool> stopped{false};
    {
      auto timer = factory(AnimationTimerParameters{
          .duration = absl::Milliseconds(200),
          .tick_callback =
              [](int64_t) { return AnimationTickResult::kContinue; },
          .stopped_callback = [&](AnimationStopReason) { stopped.store(true); },
      });
      ASSERT_NE(timer, nullptr);
      timer->Cancel();
    }
    EXPECT_TRUE(stopped.load()) << "iteration " << i;
  }
}

TEST(DisplayLinkTimerTest,
     MainThreadRunsRealVsyncTicksMonotonicallyAndFinishes) {
  std::vector<int64_t> elapsed_samples;
  absl::Notification done;
  AnimationStopReason reason = AnimationStopReason::kCancelled;

  AnimationTimerParameters params{
      .duration = absl::Milliseconds(200),
      .fire_immediately = false,
      .tick_callback =
          [&](int64_t elapsed_ns) {
            elapsed_samples.push_back(elapsed_ns);
            return elapsed_samples.size() >= 3 ? AnimationTickResult::kFinish
                                               : AnimationTickResult::kContinue;
          },
      .stopped_callback =
          [&](AnimationStopReason stop_reason) {
            reason = stop_reason;
            done.Notify();
          },
  };

  auto maybe_timer = CreateDisplayLinkTimer(params);
  if (maybe_timer.status().code() == absl::StatusCode::kUnavailable) {
    GTEST_SKIP() << maybe_timer.status();
  }
  ASSERT_TRUE(maybe_timer.ok()) << maybe_timer.status();
  std::unique_ptr<AnimationTimer> timer = *std::move(maybe_timer);

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(2)));
  EXPECT_EQ(reason, AnimationStopReason::kFinished);
  ASSERT_GE(elapsed_samples.size(), 3u);
  EXPECT_GE(elapsed_samples.front(), 0);
  for (std::size_t i = 1; i < elapsed_samples.size(); ++i) {
    EXPECT_GE(elapsed_samples[i], elapsed_samples[i - 1]) << "tick " << i;
  }
  EXPECT_GT(elapsed_samples.back(), elapsed_samples.front());
}

TEST(DisplayLinkTimerTest,
     MainThreadMidFlightCancelReportsCancelledAndSupportsDoubleCancel) {
  std::atomic<int> ticks{0};
  std::atomic<int> stop_count{0};
  absl::Notification first_tick;
  absl::Notification done;
  AnimationStopReason reason = AnimationStopReason::kFinished;

  AnimationTimerParameters params{
      .duration = absl::Seconds(2),
      .fire_immediately = false,
      .tick_callback =
          [&](int64_t) {
            if (ticks.fetch_add(1) == 0) {
              first_tick.Notify();
            }
            return AnimationTickResult::kContinue;
          },
      .stopped_callback =
          [&](AnimationStopReason stop_reason) {
            reason = stop_reason;
            stop_count.fetch_add(1);
            done.Notify();
          },
  };

  auto maybe_timer = CreateDisplayLinkTimer(params);
  if (maybe_timer.status().code() == absl::StatusCode::kUnavailable) {
    GTEST_SKIP() << maybe_timer.status();
  }
  ASSERT_TRUE(maybe_timer.ok()) << maybe_timer.status();
  std::unique_ptr<AnimationTimer> timer = *std::move(maybe_timer);

  ASSERT_TRUE(first_tick.WaitForNotificationWithTimeout(absl::Seconds(2)));
  timer->Cancel();
  timer->Cancel();

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(2)));
  EXPECT_EQ(stop_count.load(), 1);
  EXPECT_EQ(reason, AnimationStopReason::kCancelled);
  EXPECT_GE(ticks.load(), 1);
}

TEST(DisplayLinkTimerTest,
     FactoryMidFlightCancelReportsCancelledAndSupportsDoubleCancel) {
  AnimationTimerFactory factory = MakeDisplayLinkTimerFactory();
  std::atomic<int> ticks{0};
  std::atomic<int> stop_count{0};
  absl::Notification first_tick;
  absl::Notification done;
  AnimationStopReason reason = AnimationStopReason::kFinished;

  auto timer = factory(AnimationTimerParameters{
      .duration = absl::Seconds(2),
      .fallback_ticks_per_second = 240,
      .fire_immediately = false,
      .tick_callback =
          [&](int64_t) {
            if (ticks.fetch_add(1) == 0) {
              first_tick.Notify();
            }
            return AnimationTickResult::kContinue;
          },
      .stopped_callback =
          [&](AnimationStopReason stop_reason) {
            reason = stop_reason;
            stop_count.fetch_add(1);
            done.Notify();
          },
  });
  ASSERT_NE(timer, nullptr);

  ASSERT_TRUE(first_tick.WaitForNotificationWithTimeout(absl::Seconds(2)));
  timer->Cancel();
  timer->Cancel();

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(2)));
  EXPECT_EQ(stop_count.load(), 1);
  EXPECT_EQ(reason, AnimationStopReason::kCancelled);
  EXPECT_GE(ticks.load(), 1);
}

}  // namespace
}  // namespace fasterswiper
