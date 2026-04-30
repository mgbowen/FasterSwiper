#include "src/hotkeys.h"

#include "src/cf-util.h"
#include "src/status-macros.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "magic_enum/magic_enum.hpp"

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

  auto hotkey = (CFDictionaryRef)CFDictionaryGetValue(hotkey_prefs, dict_key);
  if (hotkey == nullptr) {
    // Not found.
    return nullptr;
  }

  if (CFGetTypeID(hotkey) != CFDictionaryGetTypeID()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Hotkey settings for HotkeyType ", magic_enum::enum_name(hotkey_type),
        " was found, but it is not a dictionary"));
  }

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

  auto enabled_ref = static_cast<CFBooleanRef>(
      CFDictionaryGetValue(hotkey_settings, CFSTR("enabled")));
  if (enabled_ref != nullptr) {
    if (CFGetTypeID(enabled_ref) != CFBooleanGetTypeID()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "For HotkeyType ", magic_enum::enum_name(hotkey_type),
          ", the value for \"enabled\" was found, but it is not a boolean"));
    }

    result.enabled = CFBooleanGetValue(enabled_ref);
  }

  auto value_dict = static_cast<CFDictionaryRef>(
      CFDictionaryGetValue(hotkey_settings, CFSTR("value")));
  if (value_dict != nullptr) {
    if (CFGetTypeID(value_dict) != CFDictionaryGetTypeID()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "For HotkeyType ", magic_enum::enum_name(hotkey_type),
          ", the value for \"value\" was found, but it is not a dictionary"));
    }

    auto parameters_array = static_cast<CFArrayRef>(
        CFDictionaryGetValue(value_dict, CFSTR("parameters")));
    if (parameters_array != nullptr) {
      if (CFGetTypeID(parameters_array) != CFArrayGetTypeID()) {
        return absl::InvalidArgumentError(
            absl::StrCat("For HotkeyType ", magic_enum::enum_name(hotkey_type),
                         ", the value for \"parameters\" was found, but it is "
                         "not an array"));
      }

      if (CFArrayGetCount(parameters_array) < 3) {
        return absl::InvalidArgumentError(absl::StrCat(
            "For HotkeyType ", magic_enum::enum_name(hotkey_type),
            ", the value for \"parameters\" does not have enough elements"));
      }

      auto key_code_ref =
          static_cast<CFNumberRef>(CFArrayGetValueAtIndex(parameters_array, 1));
      if (key_code_ref != nullptr) {
        if (CFGetTypeID(key_code_ref) != CFNumberGetTypeID()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "For HotkeyType ", magic_enum::enum_name(hotkey_type),
              ", the key code value for \"parameters\" is not a number"));
        }

        CFNumberGetValue(key_code_ref, kCFNumberIntType, &result.key_code);
      }

      auto modifiers_ref =
          static_cast<CFNumberRef>(CFArrayGetValueAtIndex(parameters_array, 2));
      if (modifiers_ref != nullptr) {
        if (CFGetTypeID(modifiers_ref) != CFNumberGetTypeID()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "For HotkeyType ", magic_enum::enum_name(hotkey_type),
              ", the modifiers value for \"parameters\" is not a number"));
        }

        CFNumberGetValue(modifiers_ref, kCFNumberIntType, &result.modifiers);
        result.modifiers &= kModifierKeyMask;
      }
    }
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
    ASSIGN_OR_RETURN(Hotkey default_hotkey,
                     DefaultHotkeyForHotkeyType(hotkey_type));
    return default_hotkey;
  }

  ASSIGN_OR_RETURN(Hotkey hotkey,
                   ParseHotkeyForHotkeyType(hotkey_type, hotkey_settings));
  return hotkey;
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
