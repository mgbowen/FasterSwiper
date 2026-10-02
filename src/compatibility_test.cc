#include "src/compatibility.h"

#include <optional>

#include "gtest/gtest.h"
#include <absl/flags/declare.h>
#include <absl/flags/flag.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

TEST(CompatibilityTest, ForceOSVersion) {
  const auto original_value = absl::GetFlag(FLAGS_force_os_version);

  absl::SetFlag(&FLAGS_force_os_version, 27);
  EXPECT_TRUE(IsMacOS27());

  absl::SetFlag(&FLAGS_force_os_version, 28);
  EXPECT_TRUE(IsMacOS27());

  absl::SetFlag(&FLAGS_force_os_version, 26);
  EXPECT_FALSE(IsMacOS27());

  absl::SetFlag(&FLAGS_force_os_version, 0);
  EXPECT_FALSE(IsMacOS27());

  absl::SetFlag(&FLAGS_force_os_version, original_value);
}

} // namespace
} // namespace fasterswiper
