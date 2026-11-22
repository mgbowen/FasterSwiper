#include "src/fp1616.h"

#include "src/compatibility.h"
#include "src/event.h"
#include "src/gesture-serialization.h"
#include "src/macos-private.h"

#include <array>
#include <optional>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <absl/status/statusor.h>
#include <gtest/gtest.h>

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

}  // namespace
}  // namespace fasterswiper
