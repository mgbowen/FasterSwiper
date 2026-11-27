#include "src/event.h"

#include "src/compatibility.h"
#include "src/macos-private.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <gtest/gtest.h>
#include <mach/mach_time.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

TEST(EventTest, MonotonicTimestampConversionAcrossTimebases) {
  constexpr std::array<mach_timebase_info_data_t, 4> kTimebases = {{
      {.numer = 125, .denom = 3},  // Apple Silicon (24 MHz)
      {.numer = 1, .denom = 1},    // Intel (1 GHz)
      {.numer = 1, .denom = 2},    // numer < denom
      {.numer = 2, .denom = 5},    // non-integer numer < denom
  }};

  for (const auto& tb : kTimebases) {
    SCOPED_TRACE(::testing::Message()
                 << "numer=" << tb.numer << " denom=" << tb.denom);
    std::atomic<uint64_t> last_ticks{0};
    constexpr uint64_t kFrozenNow = 10'000'000ULL;

    uint64_t prev_ticks = 0;
    uint64_t prev_ns = 0;
    uint64_t prev_roundtrip_ticks = 0;

    for (int i = 0; i < 16; ++i) {
      const uint64_t ticks =
          AdvanceMonotonicMachTimestamp(kFrozenNow, tb, last_ticks);
      const uint64_t ns = MachTicksToNanoseconds(ticks, tb);
      const uint64_t roundtrip_ticks = NanosecondsToMachTicks(ns, tb);

      EXPECT_GT(ticks, prev_ticks) << "i=" << i;
      EXPECT_GT(ns, prev_ns) << "i=" << i;
      EXPECT_GT(roundtrip_ticks, prev_roundtrip_ticks) << "i=" << i;
      if (tb.numer >= tb.denom) {
        EXPECT_EQ(roundtrip_ticks, ticks) << "i=" << i;
      }

      prev_ticks = ticks;
      prev_ns = ns;
      prev_roundtrip_ticks = roundtrip_ticks;
    }
  }
}

TEST(EventTest, NormalizePhysicalDockSwipeProgressOnMacOS26And27) {
  const absl::FlagSaver flag_saver;

  // On macOS 26, physical swipe progress is unchanged for both axes regardless
  // of natural scrolling.
  absl::SetFlag(&FLAGS_force_os_version, 26);
  for (bool natural : {false, true}) {
    EXPECT_DOUBLE_EQ(NormalizePhysicalDockSwipeProgress(
                         kCGGestureMotionHorizontal, 0.4, natural),
                     0.4);
    EXPECT_DOUBLE_EQ(NormalizePhysicalDockSwipeProgress(
                         kCGGestureMotionHorizontal, -0.4, natural),
                     -0.4);
    EXPECT_DOUBLE_EQ(NormalizePhysicalDockSwipeProgress(
                         kCGGestureMotionVertical, 0.4, natural),
                     0.4);
    EXPECT_DOUBLE_EQ(NormalizePhysicalDockSwipeProgress(
                         kCGGestureMotionVertical, -0.4, natural),
                     -0.4);
  }

  // On macOS 27, only vertical physical swipe progress is inverted when natural
  // scrolling is enabled.
  absl::SetFlag(&FLAGS_force_os_version, 27);
  EXPECT_DOUBLE_EQ(NormalizePhysicalDockSwipeProgress(
                       kCGGestureMotionHorizontal, 0.4, false),
                   0.4);
  EXPECT_DOUBLE_EQ(
      NormalizePhysicalDockSwipeProgress(kCGGestureMotionHorizontal, 0.4, true),
      0.4);
  EXPECT_DOUBLE_EQ(
      NormalizePhysicalDockSwipeProgress(kCGGestureMotionVertical, 0.4, false),
      0.4);
  EXPECT_DOUBLE_EQ(
      NormalizePhysicalDockSwipeProgress(kCGGestureMotionVertical, -0.4, false),
      -0.4);
  EXPECT_DOUBLE_EQ(
      NormalizePhysicalDockSwipeProgress(kCGGestureMotionVertical, -0.4, true),
      0.4);
  EXPECT_DOUBLE_EQ(
      NormalizePhysicalDockSwipeProgress(kCGGestureMotionVertical, 0.4, true),
      -0.4);
}

TEST(EventTest, CreateDockControlGestureEventNaturalScrollingByAxisAndOS) {
  const absl::FlagSaver flag_saver;

  absl::SetFlag(&FLAGS_force_os_version, 26);
  for (bool natural : {false, true}) {
    CFUniquePtr<CGEventRef> horiz = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionHorizontal, 1.0, 8.0, natural);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(horiz.get(), kCGEventGestureSwipeProgress),
        1.0);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(horiz.get(), kCGEventGestureSwipeVelocityX),
        8.0);

    CFUniquePtr<CGEventRef> vert = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, natural);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(vert.get(), kCGEventGestureSwipeProgress),
        1.0);
    EXPECT_DOUBLE_EQ(
        CGEventGetDoubleValueField(vert.get(), kCGEventGestureSwipeVelocityY),
        6.0);
  }

  absl::SetFlag(&FLAGS_force_os_version, 27);
  {
    CFUniquePtr<CGEventRef> horiz_no_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionHorizontal, 1.0, 8.0, false);
    CFUniquePtr<CGEventRef> horiz_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionHorizontal, 1.0, 8.0, true);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(horiz_no_nat.get(),
                                                kCGEventGestureSwipeProgress),
                     1.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(horiz_no_nat.get(),
                                                kCGEventGestureSwipeVelocityX),
                     8.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(horiz_nat.get(),
                                                kCGEventGestureSwipeProgress),
                     -1.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(horiz_nat.get(),
                                                kCGEventGestureSwipeVelocityX),
                     -8.0);

    CFUniquePtr<CGEventRef> vert_no_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, false);
    CFUniquePtr<CGEventRef> vert_nat = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, 1.0, 6.0, true);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(vert_no_nat.get(),
                                                kCGEventGestureSwipeProgress),
                     1.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(vert_no_nat.get(),
                                                kCGEventGestureSwipeVelocityY),
                     6.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(vert_nat.get(),
                                                kCGEventGestureSwipeProgress),
                     1.0);
    EXPECT_DOUBLE_EQ(CGEventGetDoubleValueField(vert_nat.get(),
                                                kCGEventGestureSwipeVelocityY),
                     6.0);
  }
}

}  // namespace
}  // namespace fasterswiper
