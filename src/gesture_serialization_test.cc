#include "src/gesture-serialization.h"

#include <ApplicationServices/ApplicationServices.h>
#include <gtest/gtest.h>

#include "src/event.h"
#include "src/macos-private.h"

namespace fasterswiper {
namespace {

TEST(GestureSerializationTest, SerializationRoundTripWithoutVelocity) {
  // 1. Create a synthetic gesture event (Phase = Began (1), Direction =
  // Horizontal (1), Progress = 0.5, no velocity)
  auto original_event = CreateDockControlGestureEvent(
      kGestureBegan, kCGGestureMotionHorizontal, 0.5, std::nullopt);
  ASSERT_NE(original_event, nullptr);

  // 2. Serialize the event
  auto serialized = SerializeGestureEvent(original_event.get());
  ASSERT_TRUE(serialized.has_value());
  EXPECT_FALSE(serialized->empty());

  // We expect the payload size of the injected 4205 field to be 68 bytes since
  // there is no velocity. The total serialization size will contain that.

  // 3. Deserialize back into a CGEventRef
  auto deserialized_event = DeserializeGestureEvent(*serialized);
  ASSERT_NE(deserialized_event, nullptr);

  // 4. Verify original fields are preserved
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(),
                                        kCGEventGesturePhase),
            kGestureBegan);
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(),
                                        kCGEventGestureSwipeMotion),
            kCGGestureMotionHorizontal);
  EXPECT_NEAR(CGEventGetDoubleValueField(deserialized_event.get(),
                                         kCGEventGestureSwipeProgress),
              0.5, 1e-5);

  // 5. Verify the helper ParseEvent parses it correctly
  auto parsed = ParseEvent(deserialized_event.get());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->source, EventSource::kSynthetic);

  ASSERT_TRUE(std::holds_alternative<DockControlEvent>(parsed->data));
  const auto &dock_event = std::get<DockControlEvent>(parsed->data);
  EXPECT_EQ(dock_event.phase, kGestureBegan);
  EXPECT_EQ(dock_event.direction, kCGGestureMotionHorizontal);
  EXPECT_NEAR(dock_event.progress, 0.5, 1e-5);
}

TEST(GestureSerializationTest, SerializationRoundTripWithVelocity) {
  // 1. Create a synthetic gesture event with velocity (Phase = Changed (2),
  // Direction = Vertical (2), Progress = 0.8, Velocity = -1.5)
  auto original_event = CreateDockControlGestureEvent(
      kGestureChanged, kCGGestureMotionVertical, 0.8, -1.5);
  ASSERT_NE(original_event, nullptr);

  // 2. Serialize the event
  auto serialized = SerializeGestureEvent(original_event.get());
  ASSERT_TRUE(serialized.has_value());
  EXPECT_FALSE(serialized->empty());

  // 3. Deserialize back into a CGEventRef
  auto deserialized_event = DeserializeGestureEvent(*serialized);
  ASSERT_NE(deserialized_event, nullptr);

  // 4. Verify fields are preserved
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(),
                                        kCGEventGesturePhase),
            kGestureChanged);
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(),
                                        kCGEventGestureSwipeMotion),
            kCGGestureMotionVertical);
  EXPECT_NEAR(CGEventGetDoubleValueField(deserialized_event.get(),
                                         kCGEventGestureSwipeProgress),
              0.8, 1e-5);
  EXPECT_NEAR(CGEventGetDoubleValueField(deserialized_event.get(),
                                         kCGEventGestureSwipeVelocityX),
              -1.5, 1e-5);

  // 5. Verify parsed representation
  auto parsed = ParseEvent(deserialized_event.get());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->source, EventSource::kSynthetic);
  ASSERT_TRUE(std::holds_alternative<DockControlEvent>(parsed->data));
  const auto &dock_event = std::get<DockControlEvent>(parsed->data);
  EXPECT_EQ(dock_event.phase, kGestureChanged);
  EXPECT_EQ(dock_event.direction, kCGGestureMotionVertical);
  EXPECT_NEAR(dock_event.progress, 0.8, 1e-5);
}

TEST(GestureSerializationTest, DeserializeCGEventDataTest) {
  auto event = CreateDockControlGestureEvent(
      kGestureChanged, kCGGestureMotionVertical, 0.8, -1.5);
  ASSERT_NE(event, nullptr);

  auto serialized = SerializeGestureEvent(event.get());
  ASSERT_TRUE(serialized.has_value());

  auto maybe_result = DeserializeCGEventData(*serialized);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status().ToString();
  const auto &result = *maybe_result;

  // Check key fields
  // kCGEventGesturePhase = 132
  EXPECT_TRUE(result.fields.contains(132));
  EXPECT_EQ(std::get<int32_t>(result.fields.at(132)), kGestureChanged);

  // kCGEventGestureSwipeMotion = 123
  EXPECT_TRUE(result.fields.contains(123));
  EXPECT_EQ(std::get<int32_t>(result.fields.at(123)), kCGGestureMotionVertical);

  // kCGEventGestureSwipeProgress = 124 (float)
  EXPECT_TRUE(result.fields.contains(124));
  EXPECT_NEAR(std::get<float>(result.fields.at(124)), 0.8f, 1e-5f);

  // kCGEventGestureSwipeVelocityX = 129 (float)
  EXPECT_TRUE(result.fields.contains(129));
  EXPECT_NEAR(std::get<float>(result.fields.at(129)), -1.5f, 1e-5f);

  // 4205 should be present as binary data (std::string)
  EXPECT_TRUE(result.fields.contains(4205));
  EXPECT_TRUE(std::holds_alternative<std::string>(result.fields.at(4205)));
  const std::string &payload = std::get<std::string>(result.fields.at(4205));
  EXPECT_EQ(payload.size(), 96);
}

TEST(GestureSerializationTest, DeserializeIOHIDSystemQueueElementTest) {
  // Test with velocity (2 nested events, 96 bytes)
  {
    auto event = CreateDockControlGestureEvent(
        kGestureChanged, kCGGestureMotionVertical, 0.8, -1.5);
    ASSERT_NE(event, nullptr);

    auto serialized = SerializeGestureEvent(event.get());
    ASSERT_TRUE(serialized.has_value());

    auto maybe_cgevent = DeserializeCGEventData(*serialized);
    ASSERT_TRUE(maybe_cgevent.ok()) << maybe_cgevent.status().ToString();
    const auto &cgevent_data = *maybe_cgevent;

    ASSERT_TRUE(cgevent_data.fields.contains(4205));
    const std::string &payload =
        std::get<std::string>(cgevent_data.fields.at(4205));
    EXPECT_EQ(payload.size(), 96);

    auto maybe_queue = DeserializeIOHIDSystemQueueElement(payload);
    ASSERT_TRUE(maybe_queue.ok()) << maybe_queue.status().ToString();
    const auto &queue = *maybe_queue;

    EXPECT_EQ(queue.header.event_count, 2);
    ASSERT_EQ(queue.events.size(), 2);

    ASSERT_TRUE(
        std::holds_alternative<IOHIDFluidTouchGestureData>(queue.events[0]));
    const auto &fluid = std::get<IOHIDFluidTouchGestureData>(queue.events[0]);
    EXPECT_EQ(fluid.base.size, 40);
    EXPECT_EQ(static_cast<uint32_t>(fluid.base.type), 23);
    EXPECT_EQ((fluid.base.options >> 24) & 0xFF, kGestureChanged);
    EXPECT_NEAR(static_cast<double>(fluid.swipe_progress) / 65536.0, 0.8, 1e-4);

    ASSERT_TRUE(std::holds_alternative<IOHIDVelocityEventData>(queue.events[1]));
    const auto &velocity = std::get<IOHIDVelocityEventData>(queue.events[1]);
    EXPECT_EQ(velocity.base.size, 28);
    EXPECT_EQ(static_cast<uint32_t>(velocity.base.type), 9);
    EXPECT_NEAR(static_cast<double>(velocity.velocity_x) / 65536.0, -1.5,
                1e-4);
  }

  // Test without velocity (1 nested event, 68 bytes)
  {
    auto event = CreateDockControlGestureEvent(
        kGestureBegan, kCGGestureMotionHorizontal, 0.5, std::nullopt);
    ASSERT_NE(event, nullptr);

    auto serialized = SerializeGestureEvent(event.get());
    ASSERT_TRUE(serialized.has_value());

    auto maybe_cgevent = DeserializeCGEventData(*serialized);
    ASSERT_TRUE(maybe_cgevent.ok()) << maybe_cgevent.status().ToString();
    const auto &cgevent_data = *maybe_cgevent;

    ASSERT_TRUE(cgevent_data.fields.contains(4205));
    const std::string &payload =
        std::get<std::string>(cgevent_data.fields.at(4205));
    EXPECT_EQ(payload.size(), 68);

    auto maybe_queue = DeserializeIOHIDSystemQueueElement(payload);
    ASSERT_TRUE(maybe_queue.ok()) << maybe_queue.status().ToString();
    const auto &queue = *maybe_queue;

    EXPECT_EQ(queue.header.event_count, 1);
    ASSERT_EQ(queue.events.size(), 1);

    ASSERT_TRUE(
        std::holds_alternative<IOHIDFluidTouchGestureData>(queue.events[0]));
    const auto &fluid = std::get<IOHIDFluidTouchGestureData>(queue.events[0]);
    EXPECT_EQ(fluid.base.size, 40);
    EXPECT_EQ(static_cast<uint32_t>(fluid.base.type), 23);
    EXPECT_EQ((fluid.base.options >> 24) & 0xFF, kGestureBegan);
    EXPECT_NEAR(static_cast<double>(fluid.swipe_progress) / 65536.0, 0.5, 1e-4);
  }

  // Test invalid payload cases (too short, corrupted header, etc.)
  {
    std::string bad_payload = "too_short";
    auto maybe_queue = DeserializeIOHIDSystemQueueElement(bad_payload);
    EXPECT_FALSE(maybe_queue.ok());
  }
}

} // namespace
} // namespace fasterswiper
