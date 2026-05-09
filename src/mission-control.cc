#include "src/mission-control.h"

#include "src/cf-util.h"
#include "src/status-macros.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <CoreGraphics/CGWindow.h>
#include <libproc.h>

#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/str_cat.h"

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
  for (auto i = 0; i < num_windows; i++) {
    VLOG(1) << "IsMissionControlVisible():  Window i=" << i;

    const auto element =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows.get(), i));
    if (CFGetTypeID(element) != CFDictionaryGetTypeID()) {
      return absl::InvalidArgumentError(
          absl::StrCat("In IsMissionControlVisible(), window element i=", i,
                       " is not a dictionary"));
    }

    const auto window_owner_name_ref = static_cast<CFStringRef>(
        CFDictionaryGetValue(element, kCGWindowOwnerName));
    if (CFGetTypeID(window_owner_name_ref) != CFStringGetTypeID()) {
      return absl::InvalidArgumentError(
          absl::StrCat("In IsMissionControlVisible(), window element i=", i,
                       ", window owner name is not a string"));
    }

    if (VLOG_IS_ON(1)) {
      VLOG(1) << "IsMissionControlVisible():    window_owner_name=\""
              << StringFromCFStringRef(window_owner_name_ref) << "\"";
    }

    if (CFStringCompare(window_owner_name_ref, CFSTR("Dock"), 0) != 0) {
      VLOG(1)
          << "IsMissionControlVisible():    window_owner_name does not match";
      continue;
    }

    VLOG(1) << "IsMissionControlVisible():    window_owner_name matches";

    // Window owner PID
    const auto window_owner_pid_ref = static_cast<CFNumberRef>(
        CFDictionaryGetValue(element, kCGWindowOwnerPID));
    if (CFGetTypeID(window_owner_pid_ref) != CFNumberGetTypeID()) {
      return absl::InvalidArgumentError(
          absl::StrCat("In IsMissionControlVisible(), window element i=", i,
                       ", window owner PID is not a number"));
    }

    int window_owner_pid;
    if (!CFNumberGetValue(window_owner_pid_ref, kCFNumberIntType,
                          &window_owner_pid)) {
      return absl::InvalidArgumentError(
          absl::StrCat("In IsMissionControlVisible(), window element i=", i,
                       ", failed to convert window_owner_pid to int"));
    }

    VLOG(1) << "IsMissionControlVisible():    window_owner_pid="
            << window_owner_pid;

    // Window owner app bundle path
    ASSIGN_OR_RETURN(const std::string app_bundle_path,
                     GetAppBundlePathForPid(window_owner_pid));
    VLOG(1) << "IsMissionControlVisible():    app_bundle_path="
            << app_bundle_path;
    if (app_bundle_path != kDockAppBundlePath) {
      VLOG(1) << "IsMissionControlVisible():    app_bundle_path does not match";
      continue;
    }

    VLOG(1) << "IsMissionControlVisible():    app_bundle_path matches";

    // Window layer
    const auto window_layer_ref =
        static_cast<CFNumberRef>(CFDictionaryGetValue(element, kCGWindowLayer));
    if (CFGetTypeID(window_layer_ref) != CFNumberGetTypeID()) {
      return absl::InvalidArgumentError(
          absl::StrCat("In IsMissionControlVisible(), window element i=", i,
                       ", window layer is not a number"));
    }

    int window_layer;
    if (!CFNumberGetValue(window_layer_ref, kCFNumberIntType, &window_layer)) {
      return absl::InvalidArgumentError(
          absl::StrCat("In IsMissionControlVisible(), window element i=", i,
                       ", failed to convert window_layer to int"));
    }

    VLOG(1) << "IsMissionControlVisible():    window_layer=" << window_layer;

    if (window_layer == 18) {
      num_layer_18++;
    } else if (window_layer == 20) {
      num_layer_20++;
    }
  }

  VLOG(1) << "IsMissionControlVisible(): num_layer_18=" << num_layer_18
          << ", num_layer_20=" << num_layer_20;

  const bool mission_control_visible =
      num_layer_18 > 0 && num_layer_20 > num_layer_18;
  VLOG(1) << "IsMissionControlVisible(): mission_control_visible="
          << mission_control_visible;
  return mission_control_visible;
}

} // namespace fasterswiper
