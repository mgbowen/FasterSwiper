#include "src/gesture-serialization.h"

#include <gtest/gtest.h>
#include <ApplicationServices/ApplicationServices.h>

#include "src/event.h"
#include "src/macos-private.h"

namespace fasterswiper {
namespace {

TEST(GestureSerializationTest, SerializationRoundTripWithoutVelocity) {
  // 1. Create a synthetic gesture event (Phase = Began (1), Direction = Horizontal (1), Progress = 0.5, no velocity)
  auto original_event = CreateDockControlGestureEvent(
      kGestureBegan, kCGGestureMotionHorizontal, 0.5, std::nullopt);
  ASSERT_NE(original_event, nullptr);

  // 2. Serialize the event
  auto serialized = SerializeGestureEvent(original_event.get());
  ASSERT_TRUE(serialized.has_value());
  EXPECT_FALSE(serialized->empty());
  
  // We expect the payload size of the injected 4205 field to be 68 bytes since there is no velocity.
  // The total serialization size will contain that.
  
  // 3. Deserialize back into a CGEventRef
  auto deserialized_event = DeserializeGestureEvent(*serialized);
  ASSERT_NE(deserialized_event, nullptr);

  // 4. Verify original fields are preserved
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(), kCGEventGesturePhase), kGestureBegan);
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(), kCGEventGestureSwipeMotion), kCGGestureMotionHorizontal);
  EXPECT_NEAR(CGEventGetDoubleValueField(deserialized_event.get(), kCGEventGestureSwipeProgress), 0.5, 1e-5);

  // 5. Verify the helper ParseEvent parses it correctly
  auto parsed = ParseEvent(deserialized_event.get());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->source, EventSource::kSynthetic);
  
  ASSERT_TRUE(std::holds_alternative<DockControlEvent>(parsed->data));
  const auto& dock_event = std::get<DockControlEvent>(parsed->data);
  EXPECT_EQ(dock_event.phase, kGestureBegan);
  EXPECT_EQ(dock_event.direction, kCGGestureMotionHorizontal);
  EXPECT_NEAR(dock_event.progress, 0.5, 1e-5);
}

TEST(GestureSerializationTest, SerializationRoundTripWithVelocity) {
  // 1. Create a synthetic gesture event with velocity (Phase = Changed (2), Direction = Vertical (2), Progress = 0.8, Velocity = -1.5)
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
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(), kCGEventGesturePhase), kGestureChanged);
  EXPECT_EQ(CGEventGetIntegerValueField(deserialized_event.get(), kCGEventGestureSwipeMotion), kCGGestureMotionVertical);
  EXPECT_NEAR(CGEventGetDoubleValueField(deserialized_event.get(), kCGEventGestureSwipeProgress), 0.8, 1e-5);
  EXPECT_NEAR(CGEventGetDoubleValueField(deserialized_event.get(), kCGEventGestureSwipeVelocityX), -1.5, 1e-5);

  // 5. Verify parsed representation
  auto parsed = ParseEvent(deserialized_event.get());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->source, EventSource::kSynthetic);
  ASSERT_TRUE(std::holds_alternative<DockControlEvent>(parsed->data));
  const auto& dock_event = std::get<DockControlEvent>(parsed->data);
  EXPECT_EQ(dock_event.phase, kGestureChanged);
  EXPECT_EQ(dock_event.direction, kCGGestureMotionVertical);
  EXPECT_NEAR(dock_event.progress, 0.8, 1e-5);
}

} // namespace
} // namespace fasterswiper
