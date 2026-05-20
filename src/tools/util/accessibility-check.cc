#include "src/tools/util/accessibility-check.h"

#include "src/cf-util.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

namespace fasterswiper {

absl::Status CheckForAccessibilityPermissions() {
  std::array<const void *, 1> keys{kAXTrustedCheckOptionPrompt};
  std::array<const void *, 1> values{kCFBooleanTrue};
  const auto opts = WrapCFUnique(CFDictionaryCreate(
      nullptr, keys.data(), values.data(), 1, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));
  const bool ok = AXIsProcessTrustedWithOptions(opts.get());
  if (!ok) {
    return absl::PermissionDeniedError(
        "macOS accessibility permissions not granted.");
  }

  return absl::OkStatus();
}

} // namespace fasterswiper
