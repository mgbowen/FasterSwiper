#pragma once

#include <absl/status/statusor.h>

namespace fasterswiper {

enum class ActiveMultitaskingWindow {
  kDesktop,
  kMissionControl,
  kAppExpose,
};

absl::StatusOr<ActiveMultitaskingWindow> GetActiveMultitaskingWindow();

} // namespace fasterswiper
