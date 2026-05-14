#include "src/mission-control.h"

#include "src/cf-collections-util.h"
#include "src/cf-util.h"
#include "src/macos-private.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>

#include "gutil/status.h"

namespace fasterswiper {

absl::StatusOr<ActiveMultitaskingWindow> GetActiveMultitaskingWindow() {
  const int cid = SLSMainConnectionID();
  auto spaces_ref =
      WrapCFUnique(SLSCopySpaces(cid, (CGSSpaceMask)(kCGSCurrentOSSpacesMask)));
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

absl::StatusOr<bool> IsMissionControlVisible() {
  ASSIGN_OR_RETURN(ActiveMultitaskingWindow multitasking_window,
                   GetActiveMultitaskingWindow());
  return multitasking_window == ActiveMultitaskingWindow::kMissionControl;
}

} // namespace fasterswiper
