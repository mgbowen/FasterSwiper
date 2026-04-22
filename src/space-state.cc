#include "src/space-state.h"

#include "src/cf-util.h"
#include "src/const.h"
#include "src/macos-private.h"

#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"

namespace fasterswiper {

namespace {

int64_t RoundNanopositions(double val) {
  return static_cast<int64_t>(std::round(val));
}

} // namespace

SpaceState::SpaceState(CFUniquePtr<CFStringRef> display_id, CFIndex index,
                       CFIndex count)
    : SpaceState(static_cast<CFSharedPtr<CFStringRef>>(std::move(display_id)),
                 index, count) {}

SpaceState::SpaceState(CFSharedPtr<CFStringRef> display_id, CFIndex index,
                       CFIndex count)
    : display_id_(std::move(display_id)), index_(index), count_(count),
      unit_factor_(static_cast<double>(count_) / (count_ - 1)) {}

int64_t SpaceState::ProgressToSwipes(double progress) const {
  return RoundNanopositions(progress / unit_factor_ * OneSwipeInNanoswipes);
}

double SpaceState::SwipesToProgress(int64_t nanopositions) const {
  return static_cast<double>(nanopositions) * unit_factor_ /
         OneSwipeInNanoswipes;
}

absl::StatusOr<SpaceState> LoadSpaceStateForActiveDisplay() {
  auto maybe_displays = GetDisplaysUnderMouse();
  if (!maybe_displays.ok()) {
    return maybe_displays.status();
  }

  if (VLOG_IS_ON(1)) {
    VLOG(1) << "Displays under mouse:";
    for (const auto &display : *maybe_displays) {
      VLOG(1) << "  * \"" << StringFromCFStringRef(display.get());
    }
  }

  const std::vector<CFUniquePtr<CFStringRef>> &displays_under_mouse =
      *maybe_displays;
  if (displays_under_mouse.empty()) {
    return absl::InternalError("No displays found under mouse");
  }

  int cid = SLSMainConnectionID();
  auto display_info_dict = WrapCFUnique(SLSCopyManagedDisplaySpaces(cid));
  if (!display_info_dict)
    return absl::InternalError("Failed to load managed display spaces.");

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

    bool found_display = false;
    for (const auto &display_under_mouse : displays_under_mouse) {
      const int result =
          CFStringCompare(raw_display_id, display_under_mouse.get(), 0);

      if (VLOG_IS_ON(1)) {
        VLOG(1) << "\"" << StringFromCFStringRef(display_under_mouse.get())
                << "\" <=> \"" << StringFromCFStringRef(raw_display_id)
                << "\" = " << result;
      }

      if (result == kCFCompareEqualTo) {
        found_display = true;
        break;
      }
    }

    if (!found_display) {
      continue;
    }

    if (VLOG_IS_ON(1)) {
      VLOG(1) << "Found display under mouse";
    }

    const uint64_t active_space_id =
        SLSManagedDisplayGetCurrentSpace(cid, raw_display_id);

    auto spaces =
        static_cast<CFArrayRef>(CFDictionaryGetValue(display, CFSTR("Spaces")));
    if (!spaces) {
      continue;
    }

    CFIndex count = CFArrayGetCount(spaces);
    for (CFIndex j = 0; j < count; j++) {
      auto space =
          static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(spaces, j));
      auto space_id_ref = static_cast<CFNumberRef>(
          CFDictionaryGetValue(space, CFSTR("ManagedSpaceID")));
      if (!space_id_ref) {
        continue;
      }

      int64_t space_id;
      CFNumberGetValue(space_id_ref, kCFNumberSInt64Type, &space_id);
      if (static_cast<uint64_t>(space_id) == active_space_id) {
        // Found the active space, retain the display ID so it isn't released
        // when we return.
        CFRetain(raw_display_id);
        return SpaceState(WrapCFUnique(raw_display_id), /*index=*/j,
                          /*count=*/count);
      }
    }
  }

  return absl::InternalError(
      "Cannot load space state for a system with zero displays.");
}

absl::StatusOr<std::vector<CFUniquePtr<CFStringRef>>> GetDisplaysUnderMouse() {
  CGEventRef raw_event = CGEventCreate(nullptr);
  if (!raw_event) {
    return absl::InternalError("Failed to create event");
  }

  auto event = WrapCFUnique(raw_event);
  const CGPoint point = CGEventGetLocation(event.get());

  // Get the count of displays first.
  uint32_t displayCount = 0;
  CGGetDisplaysWithPoint(point, 128, nullptr, &displayCount);

  // Then actually load the display IDs.
  std::vector<CGDirectDisplayID> direct_display_ids(displayCount);
  CGGetDisplaysWithPoint(point, displayCount, direct_display_ids.data(),
                         &displayCount);

  std::vector<CFUniquePtr<CFStringRef>> display_ids;
  display_ids.reserve(displayCount);

  for (uint32_t i = 0; i < displayCount; i++) {
    const CGDirectDisplayID direct_display_id = direct_display_ids[i];
    CFUniquePtr<CFUUIDRef> display_uuid_object =
        WrapCFUnique(CGDisplayCreateUUIDFromDisplayID(direct_display_id));
    if (!display_uuid_object) {
      return absl::InternalError("Failed to create display UUID object");
    }

    CFUniquePtr<CFStringRef> display_uuid_string =
        WrapCFUnique(CFUUIDCreateString(nullptr, display_uuid_object.get()));
    if (!display_uuid_string) {
      return absl::InternalError("Failed to create display UUID string");
    }

    display_ids.push_back(std::move(display_uuid_string));
  }

  return display_ids;
}

} // namespace fasterswiper
