#include "src/compatibility.h"

#include <optional>

#include <absl/flags/flag.h>

ABSL_FLAG(std::optional<int>, force_os_version, std::nullopt,
          "Override the OS version for compatibility checks.");

namespace fasterswiper {

bool IsMacOS27() {
  const std::optional<int> forced = absl::GetFlag(FLAGS_force_os_version);
  if (forced.has_value()) {
    return *forced >= 27;
  }

  if (__builtin_available(macOS 27.0, *)) {
    return true;
  }

  return false;
}

} // namespace fasterswiper
