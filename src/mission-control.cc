#include "src/mission-control.h"

#include "src/cf-collections-util.h"
#include "src/cf-util.h"
#include "src/macos-private.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <CoreGraphics/CGDirectDisplay.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGWindow.h>

#include <optional>
#include <vector>

#include <absl/base/nullability.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/status/statusor.h>

namespace fasterswiper {

namespace {

// Window layers used by macOS 27's WindowManager for overviews.
constexpr int32_t kOverviewOverlayWindowLayer = 19;
constexpr int32_t kSpacesBarWindowLayer = 14;
constexpr int32_t kShowDesktopOverlayWindowLayer = 18;

struct OverviewWindowMarkers {
  // Indicates either Mission Control or App Expose is visible
  bool overview_overlay = false;

  // Indicates only Mission Control is visible
  bool spaces_bar = false;

  ActiveMultitaskingWindow active_multitasking_window() const {
    if (!overview_overlay) {
      return ActiveMultitaskingWindow::kDesktop;
    }

    return spaces_bar ? ActiveMultitaskingWindow::kMissionControl
                      : ActiveMultitaskingWindow::kAppExpose;
  }
};

// Returns the bounds of all active displays, or an error if the query fails.
absl::StatusOr<std::vector<CGRect>> GetActiveDisplayBounds() {
  uint32_t count = 0;
  if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess ||
      count == 0) {
    return absl::InternalError("Failed to get active display count");
  }

  std::vector<CGDirectDisplayID> displays(count);
  if (CGGetActiveDisplayList(count, displays.data(), &count) !=
      kCGErrorSuccess) {
    return absl::InternalError("Failed to get active display list");
  }

  std::vector<CGRect> bounds;
  bounds.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    bounds.push_back(CGDisplayBounds(displays[i]));
  }

  return bounds;
}

// Returns true when `rect` matches the size of any display in
// `multi_display_bounds`.
bool IsDisplaySized(CGRect rect,
                    const std::vector<CGRect> &multi_display_bounds) {
  for (const auto &display_bounds : multi_display_bounds) {
    // Some minor fudge factor.
    if ((rect.size.width + 1) >= display_bounds.size.width &&
        (rect.size.height + 1) >= display_bounds.size.height) {
      return true;
    }
  }

  return false;
}

// Scans the on-screen window list for WindowManager overlay markers.
absl::StatusOr<OverviewWindowMarkers> ScanOverviewWindows() {
  auto window_list = WrapCFUnique(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
  if (window_list == nullptr) {
    return absl::InternalError("Failed to copy on-screen window list");
  }

  std::optional<std::vector<CGRect>> maybe_display_bounds;

  OverviewWindowMarkers markers;
  const CFIndex count = CFArrayGetCount(window_list.get());

  for (CFIndex i = 0; i < count; ++i) {
    ASSIGN_OR_RETURN(absl_nonnull auto window,
                     CFArrayGetAs<CFDictionaryRef>(window_list.get(), i));

    ASSIGN_OR_RETURN(std::optional<int32_t> maybe_layer,
                     CFDictOptionalGetAs<int32_t>(window, kCGWindowLayer));
    if (!maybe_layer.has_value()) {
      continue;
    }

    ASSIGN_OR_RETURN(absl_nullable auto owner, CFDictOptionalGetAs<CFStringRef>(
                                                   window, kCGWindowOwnerName));
    if (owner == nullptr || !CFEqual(owner, CFSTR("WindowManager"))) {
      continue;
    }

    const int32_t layer = *maybe_layer;
    if (layer == kSpacesBarWindowLayer) {
      markers.spaces_bar = true;
      continue;
    }

    if (layer != kOverviewOverlayWindowLayer &&
        layer != kShowDesktopOverlayWindowLayer) {
      continue;
    }

    ASSIGN_OR_RETURN(
        absl_nullable auto bounds_dict,
        CFDictOptionalGetAs<CFDictionaryRef>(window, kCGWindowBounds));
    if (bounds_dict == nullptr) {
      continue;
    }

    CGRect bounds{};
    if (!CGRectMakeWithDictionaryRepresentation(bounds_dict, &bounds)) {
      continue;
    }

    if (!maybe_display_bounds.has_value()) {
      ASSIGN_OR_RETURN(maybe_display_bounds, GetActiveDisplayBounds());
    }

    if (!IsDisplaySized(bounds, *maybe_display_bounds)) {
      continue;
    }

    if (layer == kOverviewOverlayWindowLayer) {
      markers.overview_overlay = true;
    }
  }

  return markers;
}

absl::StatusOr<ActiveMultitaskingWindow> GetActiveMultitaskingWindow_MacOS26() {
  const int cid = SLSMainConnectionID();
  auto spaces_ref =
      WrapCFUnique(SLSCopySpaces(cid, CGSSpaceMask::kCGSCurrentOSSpacesMask));
  if (spaces_ref == nullptr) {
    return absl::InternalError("Failed to load space IDs");
  }

  const CFIndex spaces_length = CFArrayGetCount(spaces_ref.get());

  bool is_mission_control_visible = false;
  bool is_app_expose_visible = false;
  for (CFIndex i = 0; i < spaces_length; i++) {
    ASSIGN_OR_RETURN(auto space_id,
                     CFArrayGetAs<SLSSpaceId>(spaces_ref.get(), i));
    auto space_name_ref = WrapCFUnique(SLSSpaceCopyName(cid, space_id));
    if (space_name_ref == nullptr) {
      continue;
    }

    if (CFEqual(space_name_ref.get(), CFSTR("mission-control"))) {
      is_mission_control_visible = true;
    } else if (CFEqual(space_name_ref.get(), CFSTR("show-front"))) {
      is_app_expose_visible = true;
    }
  }

  if (is_mission_control_visible && is_app_expose_visible) {
    return absl::InternalError(
        "Detected both Mission Control and App Expose as visible");
  }

  if (is_mission_control_visible) {
    return ActiveMultitaskingWindow::kMissionControl;
  }

  if (is_app_expose_visible) {
    return ActiveMultitaskingWindow::kAppExpose;
  }

  return ActiveMultitaskingWindow::kDesktop;
}

absl::StatusOr<ActiveMultitaskingWindow> GetActiveMultitaskingWindow_MacOS27() {
  ASSIGN_OR_RETURN(const auto markers, ScanOverviewWindows());
  return markers.active_multitasking_window();
}

} // namespace

absl::StatusOr<ActiveMultitaskingWindow> GetActiveMultitaskingWindow() {
  if (!__builtin_available(macOS 27.0, *)) {
    return GetActiveMultitaskingWindow_MacOS26();
  }

  return GetActiveMultitaskingWindow_MacOS27();
}

} // namespace fasterswiper
