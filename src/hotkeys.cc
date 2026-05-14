#include "src/hotkeys.h"

#include "src/cf-collections-util.h"
#include "src/cf-util.h"

#include <CoreGraphics/CGRemoteOperation.h>
#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <gutil/status.h>
#include <magic_enum/magic_enum.hpp>

namespace fasterswiper {

namespace {

absl::StatusOr<CFUniquePtr<CFDictionaryRef>> LoadAllHotkeysSettings() {
  auto all_hotkey_settings =
      WrapCFUnique(static_cast<CFDictionaryRef>(CFPreferencesCopyAppValue(
          CFSTR("AppleSymbolicHotKeys"), CFSTR("com.apple.symbolichotkeys"))));
  if (all_hotkey_settings == nullptr) {
    return absl::NotFoundError("Failed to find all hotkey settings");
  }

  if (CFGetTypeID(all_hotkey_settings.get()) != CFDictionaryGetTypeID()) {
    return absl::InvalidArgumentError(
        "All hotkey settings were found, but it is not a dictionary");
  }

  return all_hotkey_settings;
}

absl_nullable CFStringRef HotkeyTypeToDictionaryKey(HotkeyType hotkey_type) {
  switch (hotkey_type) {
    using enum HotkeyType;
  case kMoveSpaceLeft:
    return CFSTR("79");
  case kMoveSpaceRight:
    return CFSTR("81");
  }

  return nullptr;
}

absl::StatusOr<absl_nullable CFDictionaryRef>
GetHotkeySettingsForHotkeyType(absl_nonnull CFDictionaryRef hotkey_prefs,
                               HotkeyType hotkey_type) {
  absl_nullable CFStringRef dict_key = HotkeyTypeToDictionaryKey(hotkey_type);
  if (dict_key == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown HotkeyType ",
                     std::underlying_type_t<HotkeyType>(hotkey_type)));
  }

  ASSIGN_OR_RETURN(absl_nullable auto hotkey,
                   CFDictOptionalGetAs<CFDictionaryRef>(hotkey_prefs, dict_key),
                   _ << "HotkeyType " << magic_enum::enum_name(hotkey_type));
  return hotkey;
}

constexpr CGKeyCode kKeyCodeLeftArrow = 123;
constexpr CGKeyCode kKeyCodeRightArrow = 124;

absl::StatusOr<Hotkey> DefaultHotkeyForHotkeyType(HotkeyType hotkey_type) {
  switch (hotkey_type) {
    using enum HotkeyType;
  case kMoveSpaceLeft:
    return Hotkey{
        .enabled = true,
        .key_code = kKeyCodeLeftArrow,
        .modifiers = kCGEventFlagMaskControl,
    };
  case kMoveSpaceRight:
    return Hotkey{
        .enabled = true,
        .key_code = kKeyCodeRightArrow,
        .modifiers = kCGEventFlagMaskControl,
    };
  }

  return absl::InvalidArgumentError(
      absl::StrCat("No default HotkeyConfiguration found for HotkeyType ",
                   magic_enum::enum_name(hotkey_type)));
}

absl::StatusOr<Hotkey>
ParseHotkeyForHotkeyType(HotkeyType hotkey_type,
                         absl_nonnull CFDictionaryRef hotkey_settings) {
  ASSIGN_OR_RETURN(Hotkey result, DefaultHotkeyForHotkeyType(hotkey_type));

  auto ctx = absl::StrCat("HotkeyType ", magic_enum::enum_name(hotkey_type));

  // "enabled" (optional boolean)
  ASSIGN_OR_RETURN(std::optional<bool> maybe_enabled,
                   CFDictOptionalGetAs<bool>(hotkey_settings, CFSTR("enabled")),
                   _ << ctx << " \"enabled\"");
  if (maybe_enabled.has_value()) {
    result.enabled = *maybe_enabled;
  }

  // "value" dict
  ASSIGN_OR_RETURN(
      absl_nullable auto value_dict,
      CFDictOptionalGetAs<CFDictionaryRef>(hotkey_settings, CFSTR("value")),
      _ << ctx << " \"value\"");
  if (value_dict == nullptr) {
    return result;
  }

  // "parameters" array inside "value"
  ASSIGN_OR_RETURN(
      absl_nullable auto parameters_array,
      CFDictOptionalGetAs<CFArrayRef>(value_dict, CFSTR("parameters")),
      _ << ctx << " \"parameters\"");
  if (parameters_array == nullptr) {
    return result;
  }

  if (CFArrayGetCount(parameters_array) < 3) {
    return absl::InvalidArgumentError(
        absl::StrCat(ctx, " \"parameters\" does not have enough elements"));
  }

  // parameters[1] = key code
  ASSIGN_OR_RETURN(std::optional<CGKeyCode> maybe_key_code,
                   CFArrayOptionalGetAs<int>(parameters_array, 1),
                   _ << ctx << " parameters[1]");
  if (maybe_key_code.has_value()) {
    result.key_code = *maybe_key_code;
  }

  // parameters[2] = modifiers
  ASSIGN_OR_RETURN(auto maybe_modifiers,
                   CFArrayOptionalGetAs<CGEventFlags>(parameters_array, 2),
                   _ << ctx << " parameters[2]");
  if (maybe_modifiers.has_value()) {
    result.modifiers &= *maybe_modifiers;
  }

  return result;
}

absl::StatusOr<Hotkey>
LoadHotkeyForHotkeyType(HotkeyType hotkey_type,
                        CFDictionaryRef all_hotkey_settings) {
  ASSIGN_OR_RETURN(
      absl_nullable CFDictionaryRef hotkey_settings,
      GetHotkeySettingsForHotkeyType(all_hotkey_settings, hotkey_type));
  if (hotkey_settings == nullptr) {
    return DefaultHotkeyForHotkeyType(hotkey_type);
  }

  return ParseHotkeyForHotkeyType(hotkey_type, hotkey_settings);
}

} // namespace

absl::StatusOr<HotkeyConfigurations> LoadHotkeyConfiguration() {
  ASSIGN_OR_RETURN(CFUniquePtr<CFDictionaryRef> all_hotkey_settings,
                   LoadAllHotkeysSettings());

  HotkeyConfigurations result;
  ASSIGN_OR_RETURN(result.move_space_left,
                   LoadHotkeyForHotkeyType(HotkeyType::kMoveSpaceLeft,
                                           all_hotkey_settings.get()));
  ASSIGN_OR_RETURN(result.move_space_right,
                   LoadHotkeyForHotkeyType(HotkeyType::kMoveSpaceRight,
                                           all_hotkey_settings.get()));

  return result;
}

} // namespace fasterswiper
