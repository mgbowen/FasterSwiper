#include "src/space-state.h"

#include <ApplicationServices/ApplicationServices.h>
#include <absl/status/status.h>
#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

TEST(SpaceStateTest, StoresDirectDisplayId) {
  SpaceState unset_state(kCGNullDirectDisplay,
                         {Space{.id = 101, .is_desktop = true}}, /*index=*/0);
  EXPECT_EQ(unset_state.display_id(), kCGNullDirectDisplay);

  constexpr CGDirectDisplayID kDisplayId = 42;
  SpaceState set_state(kDisplayId, {Space{.id = 101, .is_desktop = true}},
                       /*index=*/0);
  EXPECT_EQ(set_state.display_id(), kDisplayId);
}

TEST(SpaceStateTest, GetCurrentSpaceIdRejectsNullDisplay) {
  auto result = GetCurrentSpaceIdForDisplay(kCGNullDirectDisplay);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace fasterswiper
