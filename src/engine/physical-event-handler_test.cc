#include "src/engine/physical-event-handler.h"

#include "src/compatibility.h"
#include "src/public/fasterswiper.pb.h"

#include <optional>

#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/flags/reflection.h>
#include <gtest/gtest.h>

ABSL_DECLARE_FLAG(std::optional<int>, force_os_version);

namespace fasterswiper {
namespace {

TEST(PhysicalEventHandlerTest, ShouldUseVerticalSpringCompensation) {
  const absl::FlagSaver flag_saver;

  proto::DaemonOptions unset_opts;
  proto::DaemonOptions enabled_opts;
  enabled_opts.set_enable_vertical_spring_compensation(true);
  proto::DaemonOptions disabled_opts;
  disabled_opts.set_enable_vertical_spring_compensation(false);

  absl::SetFlag(&FLAGS_force_os_version, 26);
  EXPECT_FALSE(ShouldUseVerticalSpringCompensation(unset_opts));
  EXPECT_FALSE(ShouldUseVerticalSpringCompensation(enabled_opts));
  EXPECT_FALSE(ShouldUseVerticalSpringCompensation(disabled_opts));

  absl::SetFlag(&FLAGS_force_os_version, 27);
  EXPECT_TRUE(ShouldUseVerticalSpringCompensation(unset_opts));
  EXPECT_TRUE(ShouldUseVerticalSpringCompensation(enabled_opts));
  EXPECT_FALSE(ShouldUseVerticalSpringCompensation(disabled_opts));
}

}  // namespace
}  // namespace fasterswiper
