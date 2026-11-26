#include "src/space-state.h"

#include "src/cf-collections-util.h"
#include "src/cf-util.h"
#include "src/engine/const.h"
#include "src/macos-private.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <absl/log/log.h>
#include <absl/log/vlog_is_on.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/status/statusor.h>

namespace fasterswiper {

namespace {

struct DisplayUnderMouse {
  CGDirectDisplayID direct_id = kCGNullDirectDisplay;
  CFUniquePtr<CFStringRef> uuid;
};

absl::StatusOr<CFUniquePtr<CFStringRef>> GetDisplayUuidString(
    CGDirectDisplayID display_id) {
  if (display_id == kCGNullDirectDisplay) {
    return absl::InvalidArgumentError("display_id is kCGNullDirectDisplay");
  }

  CFUniquePtr<CFUUIDRef> display_uuid_object =
      WrapCFUnique(CGDisplayCreateUUIDFromDisplayID(display_id));
  if (!display_uuid_object) {
    return absl::InternalError("Failed to create display UUID object");
  }

  CFUniquePtr<CFStringRef> display_uuid_string =
      WrapCFUnique(CFUUIDCreateString(nullptr, display_uuid_object.get()));
  if (!display_uuid_string) {
    return absl::InternalError("Failed to create display UUID string");
  }

  return display_uuid_string;
}

absl::StatusOr<std::vector<CGDirectDisplayID>> GetDirectDisplayIdsUnderMouse() {
  CGEventRef raw_event = CGEventCreate(nullptr);
  if (!raw_event) {
    return absl::InternalError("Failed to create event");
  }

  auto event = WrapCFUnique(raw_event);
  const CGPoint point = CGEventGetLocation(event.get());

  uint32_t display_count = 0;
  CGGetDisplaysWithPoint(point, 128, nullptr, &display_count);
  if (display_count == 0) {
    return std::vector<CGDirectDisplayID>{};
  }

  std::vector<CGDirectDisplayID> direct_display_ids(display_count);
  CGGetDisplaysWithPoint(point, display_count, direct_display_ids.data(),
                         &display_count);
  direct_display_ids.resize(display_count);
  return direct_display_ids;
}

absl::StatusOr<std::vector<DisplayUnderMouse>>
GetDisplaysUnderMouseWithDirectIds() {
  ASSIGN_OR_RETURN(std::vector<CGDirectDisplayID> direct_display_ids,
                   GetDirectDisplayIdsUnderMouse());

  std::vector<DisplayUnderMouse> displays;
  displays.reserve(direct_display_ids.size());

  for (const CGDirectDisplayID direct_display_id : direct_display_ids) {
    ASSIGN_OR_RETURN(CFUniquePtr<CFStringRef> display_uuid_string,
                     GetDisplayUuidString(direct_display_id));
    displays.push_back(DisplayUnderMouse{
        .direct_id = direct_display_id,
        .uuid = std::move(display_uuid_string),
    });
  }

  return displays;
}

int64_t RoundNanopositions(double val) {
  return static_cast<int64_t>(std::round(val));
}

}  // namespace

SpaceState::SpaceState(CGDirectDisplayID display_id, std::vector<Space> spaces,
                       int64_t index)
    : display_id_(display_id),
      spaces_(std::move(spaces)),
      index_(index),
      unit_factor_(count() <= 1 ? 1.0
                                : static_cast<double>(count()) /
                                      static_cast<double>(count() - 1)) {}

std::vector<int64_t> SpaceState::space_ids() const {
  std::vector<int64_t> ids;
  ids.reserve(spaces_.size());
  for (const auto& space : spaces_) {
    ids.push_back(space.id);
  }

  return ids;
}

int64_t SpaceState::ProgressToSwipes(double progress) const {
  return RoundNanopositions(progress / unit_factor_ * kOneSwipeInNanoswipes);
}

double SpaceState::SwipesToProgress(int64_t nanopositions) const {
  return static_cast<double>(nanopositions) * unit_factor_ /
         kOneSwipeInNanoswipes;
}

absl::StatusOr<SpaceState> LoadSpaceStateForActiveDisplay() {
  auto maybe_displays = GetDisplaysUnderMouseWithDirectIds();
  if (!maybe_displays.ok()) {
    return maybe_displays.status();
  }

  if (VLOG_IS_ON(1)) {
    VLOG(1) << "Displays under mouse:";
    for (const auto& display : *maybe_displays) {
      VLOG(1) << "  * \"" << StringFromCFStringRef(display.uuid.get());
    }
  }

  const std::vector<DisplayUnderMouse>& displays_under_mouse = *maybe_displays;
  if (displays_under_mouse.empty()) {
    return absl::InternalError("No displays found under mouse");
  }

  int cid = SLSMainConnectionID();
  auto display_info_dict = WrapCFUnique(SLSCopyManagedDisplaySpaces(cid));
  if (!display_info_dict) {
    return absl::InternalError("Failed to load managed display spaces.");
  }

  for (CFIndex i = 0; i < CFArrayGetCount(display_info_dict.get()); i++) {
    auto display = static_cast<CFDictionaryRef>(
        CFArrayGetValueAtIndex(display_info_dict.get(), i));

    auto raw_display_id = static_cast<CFStringRef>(
        CFDictionaryGetValue(display, CFSTR("Display Identifier")));
    if (!raw_display_id) {
      return absl::InternalError("Failed to get display ID");
    }

    if (VLOG_IS_ON(1)) {
      VLOG(1) << "Checking display from dict \""
              << StringFromCFStringRef(raw_display_id) << "\"";
    }

    std::optional<CGDirectDisplayID> matched_direct_display_id;
    for (const auto& display_under_mouse : displays_under_mouse) {
      if (CFStringCompare(raw_display_id, display_under_mouse.uuid.get(), 0) ==
          kCFCompareEqualTo) {
        matched_direct_display_id = display_under_mouse.direct_id;
        break;
      }
    }

    if (!matched_direct_display_id.has_value()) {
      continue;
    }

    const uint64_t active_space_id =
        SLSManagedDisplayGetCurrentSpace(cid, raw_display_id);

    auto spaces_array =
        static_cast<CFArrayRef>(CFDictionaryGetValue(display, CFSTR("Spaces")));
    if (!spaces_array) {
      continue;
    }

    std::vector<Space> spaces;
    std::optional<int64_t> index;

    CFIndex count = CFArrayGetCount(spaces_array);
    for (CFIndex j = 0; j < count; j++) {
      auto space_dict =
          static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(spaces_array, j));

      Space space{};
      ASSIGN_OR_RETURN(
          space.id, CFDictGetAs<int64_t>(space_dict, CFSTR("ManagedSpaceID")));
      ASSIGN_OR_RETURN(const int64_t space_type,
                       CFDictGetAs<int64_t>(space_dict, CFSTR("type")));
      space.is_desktop = space_type == kCGSSpaceUser;

      spaces.push_back(space);

      if (static_cast<uint64_t>(space.id) == active_space_id) {
        index = j;
      }
    }

    if (index.has_value()) {
      return SpaceState(*matched_direct_display_id, std::move(spaces),
                        /*index=*/*index);
    }
  }

  return absl::InternalError(
      "Cannot load space state for a system with zero displays.");
}

absl::StatusOr<int64_t> GetCurrentSpaceIdForDisplay(
    CGDirectDisplayID display_id) {
  ASSIGN_OR_RETURN(CFUniquePtr<CFStringRef> uuid,
                   GetDisplayUuidString(display_id));
  return SLSManagedDisplayGetCurrentSpace(SLSMainConnectionID(), uuid.get());
}

std::optional<CGDirectDisplayID> GetPrimaryDisplayUnderMouse() {
  auto maybe_ids = GetDirectDisplayIdsUnderMouse();
  if (!maybe_ids.ok() || maybe_ids->empty() ||
      (*maybe_ids)[0] == kCGNullDirectDisplay) {
    return std::nullopt;
  }
  return (*maybe_ids)[0];
}

}  // namespace fasterswiper
