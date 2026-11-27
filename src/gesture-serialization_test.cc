#include "src/gesture-serialization.h"

#include "src/cf-util.h"
#include "src/compatibility.h"
#include "src/event.h"
#include "src/fp1616.h"
#include "src/macos-private.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <gtest/gtest.h>
#include <mach/mach_time.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

absl::StatusOr<IOHIDSystemQueueElementData> ExtractField4205(CGEventRef event) {
  absl::StatusOr<CGEventData> event_data = DeserializeCGEventData(event);
  if (!event_data.ok()) {
    return event_data.status();
  }
  if (!event_data->fields.contains(4205)) {
    return absl::NotFoundError("Field 4205 missing from CGEventData");
  }
  const auto* blob = std::get_if<std::string>(&event_data->fields.at(4205));
  if (blob == nullptr) {
    return absl::InvalidArgumentError("Field 4205 is not a binary blob");
  }
  return DeserializeIOHIDSystemQueueElementData(*blob);
}

TEST(GestureSerializationTest, MatchesField4205WireSerialization) {
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

    ASSERT_TRUE(AugmentCGEvent(event.get()).ok());
    absl::StatusOr<IOHIDSystemQueueElementData> wire =
        ExtractField4205(event.get());
    ASSERT_TRUE(wire.ok()) << wire.status();
    ASSERT_EQ(wire->events.size(), 2u);
    const auto& wire_gesture =
        std::get<IOHIDFluidTouchGestureData>(wire->events[0]);
    const auto& wire_velocity =
        std::get<IOHIDVelocityEventData>(wire->events[1]);
    EXPECT_EQ(wire_gesture.swipe_progress, gesture.swipe_progress)
        << "val=" << val;
    EXPECT_EQ(wire_velocity.velocity_y, velocity.velocity_y) << "val=" << val;
  }
}

TEST(GestureSerializationTest,
     AugmentCGEventMutatesInPlaceWithField4205WireParity) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
      kGestureEnded, kCGGestureMotionHorizontal, 0.375, -12.5,
      /*natural_scrolling_enabled=*/false);
  ASSERT_NE(event, nullptr);
  CGEventSetDoubleValueField(event.get(), kCGEventGestureSwipePositionX, 0.25);
  CGEventSetDoubleValueField(event.get(), kCGEventGestureSwipePositionY, -0.5);
  CGEventSetIntegerValueField(
      event.get(), kCGEventGestureSwipeMask,
      static_cast<int64_t>(IOHIDSwipeMask::kIOHIDSwipeLeft));

  // Unaugmented event has no attached IOHIDEvent and no field 4205.
  EXPECT_EQ(WrapCFUnique(CGEventCopyIOHIDEvent(event.get())), nullptr);
  {
    absl::StatusOr<CGEventData> before = DeserializeCGEventData(event.get());
    ASSERT_TRUE(before.ok()) << before.status();
    EXPECT_FALSE(before->fields.contains(4205));
  }

  absl::StatusOr<IOHIDSystemQueueElementData> expected =
      GenerateIOHIDSystemQueueElementDataFromCGEvent(event.get());
  ASSERT_TRUE(expected.ok()) << expected.status();

  const uint64_t outer_ts_before = CGEventGetTimestamp(event.get());

  const absl::Status status = AugmentCGEvent(event.get());
  ASSERT_TRUE(status.ok()) << status;

  // Mutated in-place: CGEventCopyIOHIDEvent now returns an attached event, and
  // outer CGEvent fields remain intact.
  EXPECT_NE(WrapCFUnique(CGEventCopyIOHIDEvent(event.get())), nullptr);
  EXPECT_EQ(CGEventGetTimestamp(event.get()), outer_ts_before);
  EXPECT_EQ(CGEventGetIntegerValueField(event.get(), kCGEventGesturePhase),
            kGestureEnded);
  EXPECT_DOUBLE_EQ(
      CGEventGetDoubleValueField(event.get(), kCGEventGestureSwipeProgress),
      0.375);
  EXPECT_DOUBLE_EQ(
      CGEventGetDoubleValueField(event.get(), kCGEventGestureSwipeVelocityX),
      -12.5);

  absl::StatusOr<IOHIDSystemQueueElementData> actual =
      ExtractField4205(event.get());
  ASSERT_TRUE(actual.ok()) << actual.status();

  EXPECT_EQ(actual->header.timestamp, expected->header.timestamp);
  EXPECT_EQ(actual->header.sender_id, expected->header.sender_id);
  EXPECT_EQ(actual->header.options, expected->header.options);
  EXPECT_EQ(actual->header.attribute_length, expected->header.attribute_length);
  EXPECT_EQ(actual->header.event_count, expected->header.event_count);
  ASSERT_EQ(actual->events.size(), 2u);

  const auto& exp_gesture =
      std::get<IOHIDFluidTouchGestureData>(expected->events[0]);
  const auto& act_gesture =
      std::get<IOHIDFluidTouchGestureData>(actual->events[0]);
  EXPECT_EQ(act_gesture.base.size, exp_gesture.base.size);
  EXPECT_EQ(act_gesture.base.type, exp_gesture.base.type);
  EXPECT_EQ(act_gesture.base.options, exp_gesture.base.options);
  EXPECT_EQ(act_gesture.base.depth, exp_gesture.base.depth);
  EXPECT_EQ(act_gesture.position_x, exp_gesture.position_x);
  EXPECT_EQ(act_gesture.position_y, exp_gesture.position_y);
  EXPECT_EQ(act_gesture.position_z, exp_gesture.position_z);
  EXPECT_EQ(act_gesture.swipe_mask, exp_gesture.swipe_mask);
  EXPECT_EQ(act_gesture.gesture_motion, exp_gesture.gesture_motion);
  EXPECT_EQ(act_gesture.gesture_flavor, exp_gesture.gesture_flavor);
  EXPECT_EQ(act_gesture.swipe_progress, exp_gesture.swipe_progress);

  const auto& exp_vel = std::get<IOHIDVelocityEventData>(expected->events[1]);
  const auto& act_vel = std::get<IOHIDVelocityEventData>(actual->events[1]);
  EXPECT_EQ(act_vel.base.size, exp_vel.base.size);
  EXPECT_EQ(act_vel.base.type, exp_vel.base.type);
  EXPECT_EQ(act_vel.base.options, exp_vel.base.options);
  EXPECT_EQ(act_vel.base.depth, exp_vel.base.depth);
  EXPECT_EQ(act_vel.velocity_x, exp_vel.velocity_x);
  EXPECT_EQ(act_vel.velocity_y, exp_vel.velocity_y);
  EXPECT_EQ(act_vel.velocity_z, exp_vel.velocity_z);

  // Re-augmenting the same CGEvent replaces the previously attached IOHIDEvent.
  constexpr uint64_t kUpdatedNs = 987'654'321'000ULL;
  CGEventSetTimestamp(event.get(), kUpdatedNs);
  CGEventSetDoubleValueField(event.get(), kCGEventGestureSwipeProgress, -0.75);
  ASSERT_TRUE(AugmentCGEvent(event.get()).ok());

  absl::StatusOr<IOHIDSystemQueueElementData> updated =
      ExtractField4205(event.get());
  ASSERT_TRUE(updated.ok()) << updated.status();
  EXPECT_EQ(updated->header.timestamp, NanosecondsToMachTicks(kUpdatedNs));
  ASSERT_EQ(updated->events.size(), 2u);
  EXPECT_EQ(
      std::get<IOHIDFluidTouchGestureData>(updated->events[0]).swipe_progress,
      DoubleToFixedFP1616(-0.75));
}

TEST(GestureSerializationTest, VelocityChildPresenceByPhaseAndVelocity) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  // kGestureEnded emits velocity child even when velocity is 0.0.
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

    ASSERT_TRUE(AugmentCGEvent(event.get()).ok());
    absl::StatusOr<IOHIDSystemQueueElementData> wire =
        ExtractField4205(event.get());
    ASSERT_TRUE(wire.ok()) << wire.status();
    ASSERT_EQ(wire->events.size(), 2u);
    ASSERT_TRUE(
        std::holds_alternative<IOHIDVelocityEventData>(wire->events[1]));
    const auto& wire_velocity =
        std::get<IOHIDVelocityEventData>(wire->events[1]);
    EXPECT_EQ(wire_velocity.base.type, IOHIDEventType::kIOHIDEventTypeVelocity);
    EXPECT_EQ(wire_velocity.base.depth, 1);
    EXPECT_EQ(wire_velocity.velocity_x, 0);
    EXPECT_EQ(wire_velocity.velocity_y, 0);
  }

  // kGestureChanged with zero or omitted velocity omits the velocity child.
  for (std::optional<double> zero_vel :
       {std::optional<double>{std::nullopt}, std::optional<double>{0.0}}) {
    CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
        kGestureChanged, kCGGestureMotionHorizontal, 0.25, zero_vel,
        /*natural_scrolling_enabled=*/false);
    ASSERT_NE(event, nullptr);

    absl::StatusOr<IOHIDSystemQueueElementData> element =
        GenerateIOHIDSystemQueueElementDataFromCGEvent(event.get());
    ASSERT_TRUE(element.ok()) << element.status();
    EXPECT_EQ(element->header.event_count, 1u);
    EXPECT_EQ(element->events.size(), 1u);

    ASSERT_TRUE(AugmentCGEvent(event.get()).ok());
    absl::StatusOr<IOHIDSystemQueueElementData> wire =
        ExtractField4205(event.get());
    ASSERT_TRUE(wire.ok()) << wire.status();
    EXPECT_EQ(wire->header.event_count, 1u);
    ASSERT_EQ(wire->events.size(), 1u);
    EXPECT_TRUE(
        std::holds_alternative<IOHIDFluidTouchGestureData>(wire->events[0]));
  }

  // kGestureChanged with non-zero velocity includes the velocity child.
  {
    CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
        kGestureChanged, kCGGestureMotionVertical, 0.5, 2.5,
        /*natural_scrolling_enabled=*/false);
    ASSERT_NE(event, nullptr);

    ASSERT_TRUE(AugmentCGEvent(event.get()).ok());
    absl::StatusOr<IOHIDSystemQueueElementData> wire =
        ExtractField4205(event.get());
    ASSERT_TRUE(wire.ok()) << wire.status();
    EXPECT_EQ(wire->header.event_count, 2u);
    ASSERT_EQ(wire->events.size(), 2u);
    ASSERT_TRUE(
        std::holds_alternative<IOHIDVelocityEventData>(wire->events[1]));
    EXPECT_EQ(std::get<IOHIDVelocityEventData>(wire->events[1]).velocity_y,
              DoubleToFixedFP1616(2.5));
  }
}

TEST(GestureSerializationTest, MonotonicTimestampsAcrossRapidBurst) {
  const absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_force_os_version, 27);

  constexpr int kBurstSize = 32;
  const uint64_t before_ticks = mach_absolute_time();
  const uint64_t before_ns = MachTicksToNanoseconds(before_ticks);

  uint64_t prev_cg_ts = 0;
  uint64_t prev_hid_ts = 0;

  for (int i = 0; i < kBurstSize; ++i) {
    CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
        kGestureEnded, kCGGestureMotionHorizontal, 0.000016, 0.0,
        /*natural_scrolling_enabled=*/false);
    ASSERT_NE(event, nullptr);

    const absl::Status status = AugmentCGEvent(event.get());
    ASSERT_TRUE(status.ok()) << status;

    const uint64_t cg_ts = CGEventGetTimestamp(event.get());
    EXPECT_GT(cg_ts, prev_cg_ts) << "i=" << i;
    prev_cg_ts = cg_ts;

    absl::StatusOr<IOHIDSystemQueueElementData> hid_data =
        ExtractField4205(event.get());
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

TEST(GestureSerializationTest,
     ExplicitNanosecondCGEventConvertsToMachTicksInField4205) {
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

  const absl::Status status = AugmentCGEvent(raw_event.get());
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(CGEventGetTimestamp(raw_event.get()), kExplicitNs);

  absl::StatusOr<IOHIDSystemQueueElementData> hid_data =
      ExtractField4205(raw_event.get());
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

  // Zero-timestamp fallback also applies when augmenting directly in-place.
  ASSERT_TRUE(AugmentCGEvent(raw_event.get()).ok());
  absl::StatusOr<IOHIDSystemQueueElementData> augmented_first =
      ExtractField4205(raw_event.get());
  ASSERT_TRUE(augmented_first.ok()) << augmented_first.status();
  EXPECT_GT(augmented_first->header.timestamp,
            fallback_second->header.timestamp);

  ASSERT_TRUE(AugmentCGEvent(raw_event.get()).ok());
  absl::StatusOr<IOHIDSystemQueueElementData> augmented_second =
      ExtractField4205(raw_event.get());
  ASSERT_TRUE(augmented_second.ok()) << augmented_second.status();
  EXPECT_GT(augmented_second->header.timestamp,
            augmented_first->header.timestamp);
}

TEST(GestureSerializationTest,
     AugmentCGEventWithExplicitElementAndErrorHandling) {
  // Empty element list is rejected without attaching an IOHIDEvent.
  {
    auto event = WrapCFUnique(CGEventCreate(nullptr));
    ASSERT_NE(event, nullptr);
    IOHIDSystemQueueElementData empty_element{};
    const absl::Status status = AugmentCGEvent(event.get(), empty_element);
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(WrapCFUnique(CGEventCopyIOHIDEvent(event.get())), nullptr);
  }

  // Mismatched event_count is rejected without attaching an IOHIDEvent.
  {
    auto event = WrapCFUnique(CGEventCreate(nullptr));
    ASSERT_NE(event, nullptr);
    IOHIDSystemQueueElementData mismatched{};
    mismatched.header.event_count = 2;
    mismatched.events.emplace_back(IOHIDFluidTouchGestureData{
        .base = {.size = sizeof(IOHIDFluidTouchGestureData),
                 .type = IOHIDEventType::kIOHIDEventTypeFluidTouchGesture,
                 .options = 0,
                 .depth = 0,
                 .reserved = {0, 0, 0}},
    });
    const absl::Status status = AugmentCGEvent(event.get(), mismatched);
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(WrapCFUnique(CGEventCopyIOHIDEvent(event.get())), nullptr);
  }

  // Malformed sub-event size causes IOHIDEventCreateWithBytes to fail and
  // leaves the CGEvent untouched.
  {
    auto event = WrapCFUnique(CGEventCreate(nullptr));
    ASSERT_NE(event, nullptr);
    IOHIDSystemQueueElementData malformed{};
    malformed.header.event_count = 1;
    malformed.events.emplace_back(IOHIDFluidTouchGestureData{
        .base = {.size = 9999,
                 .type = IOHIDEventType::kIOHIDEventTypeFluidTouchGesture,
                 .options = 0,
                 .depth = 0,
                 .reserved = {0, 0, 0}},
    });
    const absl::Status status = AugmentCGEvent(event.get(), malformed);
    EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
    EXPECT_EQ(WrapCFUnique(CGEventCopyIOHIDEvent(event.get())), nullptr);
  }

  // Valid explicit element succeeds and round-trips through field 4205.
  {
    auto event = WrapCFUnique(CGEventCreate(nullptr));
    ASSERT_NE(event, nullptr);
    CGEventSetIntegerValueField(event.get(), kCGSEventTypeField,
                                static_cast<int64_t>(kCGSEventDockControl));
    IOHIDSystemQueueElementData valid{};
    valid.header = {.timestamp = 424242,
                    .sender_id = 0,
                    .options = 0,
                    .attribute_length = 0,
                    .event_count = 1};
    valid.events.emplace_back(IOHIDFluidTouchGestureData{
        .base = {.size = sizeof(IOHIDFluidTouchGestureData),
                 .type = IOHIDEventType::kIOHIDEventTypeFluidTouchGesture,
                 .options = static_cast<uint32_t>(kGestureBegan << 24),
                 .depth = 0,
                 .reserved = {0, 0, 0}},
        .position_x = 0,
        .position_y = 0,
        .position_z = 0,
        .swipe_mask = IOHIDSwipeMask::kIOHIDUnknown,
        .gesture_motion = IOHIDGestureMotion::kIOHIDGestureMotionHorizontalX,
        .gesture_flavor = IOHIDGestureFlavor::kIOHIDGestureFlavorDockPrimary,
        .swipe_progress = DoubleToFixedFP1616(0.5),
    });
    ASSERT_TRUE(AugmentCGEvent(event.get(), valid).ok());
    EXPECT_NE(WrapCFUnique(CGEventCopyIOHIDEvent(event.get())), nullptr);
    absl::StatusOr<IOHIDSystemQueueElementData> wire =
        ExtractField4205(event.get());
    ASSERT_TRUE(wire.ok()) << wire.status();
    EXPECT_EQ(wire->header.timestamp, 424242u);
    ASSERT_EQ(wire->events.size(), 1u);
    EXPECT_EQ(
        std::get<IOHIDFluidTouchGestureData>(wire->events[0]).swipe_progress,
        DoubleToFixedFP1616(0.5));
  }
}

}  // namespace
}  // namespace fasterswiper
