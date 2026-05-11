#include "src/mission-control.h"

#include "src/cf-collections-util.h"
#include "src/cf-util.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <CoreGraphics/CGWindow.h>
#include <libproc.h>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gutil/status.h"

namespace fasterswiper {

namespace {

constexpr absl::string_view kDockAppBundlePath =
    "/System/Library/CoreServices/Dock.app/Contents/MacOS/Dock";

absl::StatusOr<std::string> GetAppBundlePathForPid(int pid) {
  std::array<char, PROC_PIDPATHINFO_MAXSIZE> buffer;
  int ret = proc_pidpath(pid, buffer.data(), buffer.size());
  if (ret <= 0) {
    return absl::InternalError(
        absl::StrCat("Failed to load pidpath for PID ", pid));
  }

  return std::string(buffer.data());
}

} // namespace

absl::StatusOr<bool> IsMissionControlVisible() {
  auto windows = WrapCFUnique(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly, kCGNullWindowID));

  int num_layer_18 = 0;
  int num_layer_20 = 0;

  const CFIndex num_windows = CFArrayGetCount(windows.get());
  for (CFIndex i = 0; i < num_windows; i++) {
    auto ctx = absl::StrCat("IsMissionControlVisible() window i=", i);
    VLOG(1) << ctx;

    ASSIGN_OR_RETURN(auto element,
                     CFArrayGetAs<CFDictionaryRef>(windows.get(), i), _ << ctx);

    // Window owner name
    ASSIGN_OR_RETURN(auto window_owner_name_ref,
                     CFDictGetAs<CFStringRef>(element, kCGWindowOwnerName),
                     _ << ctx << " owner name");
    VLOG(1) << ctx << " owner_name=\""
            << StringFromCFStringRef(window_owner_name_ref) << "\"";

    if (CFStringCompare(window_owner_name_ref, CFSTR("Dock"), 0) != 0) {
      continue;
    }

    // Window owner PID
    ASSIGN_OR_RETURN(const auto window_owner_pid,
                     CFDictGetAs<int>(element, kCGWindowOwnerPID),
                     _ << ctx << " owner PID");
    VLOG(1) << ctx << " owner_pid=" << window_owner_pid;

    // Window owner app bundle path
    ASSIGN_OR_RETURN(const std::string app_bundle_path,
                     GetAppBundlePathForPid(window_owner_pid));
    VLOG(1) << ctx << " app_bundle_path=" << app_bundle_path;
    if (app_bundle_path != kDockAppBundlePath) {
      continue;
    }

    // Window layer
    ASSIGN_OR_RETURN(const auto window_layer,
                     CFDictGetAs<int>(element, kCGWindowLayer),
                     _ << ctx << " layer");
    VLOG(1) << ctx << " layer=" << window_layer;

    if (window_layer == 18) {
      num_layer_18++;
    } else if (window_layer == 20) {
      num_layer_20++;
    }
  }

  const bool visible = num_layer_18 > 0 && num_layer_20 > num_layer_18;
  VLOG(1) << "IsMissionControlVisible(): layer_18=" << num_layer_18
          << " layer_20=" << num_layer_20 << " visible=" << visible;
  return visible;
}

} // namespace fasterswiper
