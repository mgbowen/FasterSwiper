#include "src/fp1616.h"

#include "src/compatibility.h"
#include "src/event.h"
#include "src/gesture-serialization.h"
#include "src/macos-private.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <absl/status/statusor.h>
#include <gtest/gtest.h>
#include <mach/mach_time.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

TEST(FP1616Test, MatchesField4205WireSerialization) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  constexpr std::array<double, 13> kTestValues = {
      0.0,
      0.000016,
      -0.000016,
      kFixedFP1616Lsb,
      -kFixedFP1616Lsb,
      0.123456,
      -0.5,
      0.99999,
      1.0,
      -1.0,
      6.0,
      -6.0,
      500.0,
  };

  for (double val : kTestValues) {
    CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionVertical, val, val,
        /*natural_scrolling_enabled=*/false);
    ASSERT_NE(event, nullptr);

    absl::StatusOr<IOHIDSystemQueueElementData> element =
        GenerateIOHIDSystemQueueElementDataFromCGEvent(event.get());
    ASSERT_TRUE(element.ok()) << element.status();
    ASSERT_EQ(element->events.size(), 2u);

    const auto& gesture =
        std::get<IOHIDFluidTouchGestureData>(element->events[0]);
    const auto& velocity = std::get<IOHIDVelocityEventData>(element->events[1]);

    EXPECT_EQ(gesture.swipe_progress, DoubleToFixedFP1616(val))
        << "val=" << val;
    EXPECT_EQ(velocity.velocity_y, DoubleToFixedFP1616(val)) << "val=" << val;
    EXPECT_DOUBLE_EQ(QuantizeFP1616(val),
                     static_cast<double>(DoubleToFixedFP1616(val)) / 65536.0);
  }
}

TEST(FP1616Test, ClampsOverflowAndHandlesNonFiniteInputs) {
  constexpr FixedFP1616 kExpectedMax = static_cast<FixedFP1616>(32767) * 65536;
  constexpr FixedFP1616 kExpectedMin = -kExpectedMax;

  EXPECT_EQ(DoubleToFixedFP1616(0.0), 0);
  EXPECT_EQ(DoubleToFixedFP1616(-0.0), 0);
  EXPECT_EQ(DoubleToFixedFP1616(1e-10), 1);
  EXPECT_EQ(DoubleToFixedFP1616(-1e-10), -1);
  EXPECT_EQ(DoubleToFixedFP1616(32000.0), 32000 * 65536);
  EXPECT_EQ(DoubleToFixedFP1616(-32000.0), -32000 * 65536);
  EXPECT_EQ(DoubleToFixedFP1616(32767.0), kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-32767.0), kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(32768.0), kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-32768.0), kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(1e9), kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-1e9), kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(std::numeric_limits<double>::infinity()),
            kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-std::numeric_limits<double>::infinity()),
            kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(std::numeric_limits<double>::quiet_NaN()), 0);
}

TEST(FP1616Test, GestureEndedWithZeroVelocityEmitsVelocityChild) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  for (bool natural_scrolling : {false, true}) {
    CFUniquePtr<CGEventRef> event =
        CreateDockControlGestureEvent(kGestureEnded, kCGGestureMotionHorizontal,
                                      0.000016, 0.0, natural_scrolling);
    ASSERT_NE(event, nullptr);

    absl::StatusOr<IOHIDSystemQueueElementData> element =
        GenerateIOHIDSystemQueueElementDataFromCGEvent(event.get());
    ASSERT_TRUE(element.ok()) << element.status();
    ASSERT_EQ(element->events.size(), 2u);
    ASSERT_TRUE(
        std::holds_alternative<IOHIDVelocityEventData>(element->events[1]));
    const auto& velocity = std::get<IOHIDVelocityEventData>(element->events[1]);
    EXPECT_EQ(velocity.base.type, IOHIDEventType::kIOHIDEventTypeVelocity);
    EXPECT_EQ(velocity.velocity_x, 0);
    EXPECT_EQ(velocity.velocity_y, 0);
  }
}

TEST(FP1616Test, MonotonicTimestampsAcrossRapidBurst) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  constexpr int kBurstSize = 32;
  const uint64_t before_ticks = mach_absolute_time();
  const uint64_t before_ns = MachTicksToNanoseconds(before_ticks);

  uint64_t prev_cg_ts = 0;
  uint64_t prev_hid_ts = 0;

  for (int i = 0; i < kBurstSize; ++i) {
    CFUniquePtr<CGEventRef> raw_event = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionHorizontal, 0.000016, 0.0,
        /*natural_scrolling_enabled=*/false);
    ASSERT_NE(raw_event, nullptr);

    absl::StatusOr<CFUniquePtr<CGEventRef>> augmented =
        AugmentCGEvent(raw_event.get());
    ASSERT_TRUE(augmented.ok()) << augmented.status();

    const uint64_t cg_ts = CGEventGetTimestamp(augmented->get());
    EXPECT_GT(cg_ts, prev_cg_ts) << "i=" << i;
    prev_cg_ts = cg_ts;

    absl::StatusOr<CGEventData> event_data =
        DeserializeCGEventData(augmented->get());
    ASSERT_TRUE(event_data.ok()) << event_data.status();
    ASSERT_TRUE(event_data->fields.contains(4205));
    const auto& blob = std::get<std::string>(event_data->fields.at(4205));
    absl::StatusOr<IOHIDSystemQueueElementData> hid_data =
        DeserializeIOHIDSystemQueueElementData(blob);
    ASSERT_TRUE(hid_data.ok()) << hid_data.status();

    EXPECT_GT(hid_data->header.timestamp, prev_hid_ts) << "i=" << i;
    EXPECT_EQ(hid_data->header.timestamp, NanosecondsToMachTicks(cg_ts))
        << "i=" << i;
    prev_hid_ts = hid_data->header.timestamp;
  }

  const uint64_t after_ticks = mach_absolute_time();
  const uint64_t after_ns = MachTicksToNanoseconds(after_ticks);

  // Outer CGEvent timestamp is in nanoseconds (near
  // MachTicksToNanoseconds(mach_absolute_time())), while field 4205 is in
  // Mach ticks (near mach_absolute_time()). Allow headroom for monotonic
  // increments during the tight burst.
  constexpr uint64_t kHeadroomTicks = 1000;
  EXPECT_GE(prev_hid_ts, before_ticks);
  EXPECT_LE(prev_hid_ts, after_ticks + kHeadroomTicks);
  EXPECT_GE(prev_cg_ts, before_ns);
  EXPECT_LE(prev_cg_ts, after_ns + MachTicksToNanoseconds(kHeadroomTicks));
}

TEST(FP1616Test, ExplicitNanosecondCGEventConvertsToMachTicksInField4205) {
  constexpr uint64_t kExplicitNs = 123'456'789'000ULL;

  auto raw_event = WrapCFUnique(CGEventCreate(nullptr));
  ASSERT_NE(raw_event, nullptr);
  CGEventSetIntegerValueField(raw_event.get(), kCGSEventTypeField,
                              static_cast<int64_t>(kCGSEventDockControl));
  CGEventSetIntegerValueField(raw_event.get(), kCGEventGesturePhase,
                              kGestureEnded);
  CGEventSetIntegerValueField(raw_event.get(), kCGEventGestureSwipeMotion,
                              kCGGestureMotionHorizontal);
  CGEventSetTimestamp(raw_event.get(), kExplicitNs);

  absl::StatusOr<IOHIDSystemQueueElementData> direct_element =
      GenerateIOHIDSystemQueueElementDataFromCGEvent(raw_event.get());
  ASSERT_TRUE(direct_element.ok()) << direct_element.status();
  EXPECT_EQ(direct_element->header.timestamp,
            NanosecondsToMachTicks(kExplicitNs));

  absl::StatusOr<CFUniquePtr<CGEventRef>> augmented =
      AugmentCGEvent(raw_event.get());
  ASSERT_TRUE(augmented.ok()) << augmented.status();
  EXPECT_EQ(CGEventGetTimestamp(augmented->get()), kExplicitNs);

  absl::StatusOr<CGEventData> event_data =
      DeserializeCGEventData(augmented->get());
  ASSERT_TRUE(event_data.ok()) << event_data.status();
  ASSERT_TRUE(event_data->fields.contains(4205));
  const auto& blob = std::get<std::string>(event_data->fields.at(4205));
  absl::StatusOr<IOHIDSystemQueueElementData> hid_data =
      DeserializeIOHIDSystemQueueElementData(blob);
  ASSERT_TRUE(hid_data.ok()) << hid_data.status();
  EXPECT_EQ(hid_data->header.timestamp, NanosecondsToMachTicks(kExplicitNs));

  // Zero outer timestamp falls back to strictly monotonic Mach ticks.
  CGEventSetTimestamp(raw_event.get(), 0);
  const uint64_t before_ticks = mach_absolute_time();
  absl::StatusOr<IOHIDSystemQueueElementData> fallback_first =
      GenerateIOHIDSystemQueueElementDataFromCGEvent(raw_event.get());
  absl::StatusOr<IOHIDSystemQueueElementData> fallback_second =
      GenerateIOHIDSystemQueueElementDataFromCGEvent(raw_event.get());
  ASSERT_TRUE(fallback_first.ok()) << fallback_first.status();
  ASSERT_TRUE(fallback_second.ok()) << fallback_second.status();
  EXPECT_GE(fallback_first->header.timestamp, before_ticks);
  EXPECT_GT(fallback_second->header.timestamp,
            fallback_first->header.timestamp);
}

TEST(FP1616Test, MonotonicTimestampConversionAcrossTimebases) {
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

}  // namespace
}  // namespace fasterswiper
