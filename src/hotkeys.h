#pragma once

#include <CoreGraphics/CGEventTypes.h>
#include <CoreGraphics/CGRemoteOperation.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace fasterswiper {

// Combined mask of all supported modifier keys (e.g. control, command, etc.).
//
// This removes extra bits that identify when special keys are pressed, e.g. the
// arrow keys. For our purposes, we're only interested in knowing if these
// specific keys are pressed.
constexpr CGEventFlags kModifierKeyMask =
    kCGEventFlagMaskAlphaShift | kCGEventFlagMaskShift |
    kCGEventFlagMaskControl | kCGEventFlagMaskAlternate |
    kCGEventFlagMaskCommand;

enum class HotkeyType {
  kMoveSpaceLeft,
  kMoveSpaceRight,
};

struct Hotkey {
  bool enabled;
  CGKeyCode key_code;
  CGEventFlags modifiers;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const Hotkey &hotkey) {
    absl::Format(&sink, "Hotkey{enabled=%s, key_code=%d, modifiers=%d}",
                 hotkey.enabled ? "true" : "false", hotkey.key_code,
                 hotkey.modifiers);
  }
};

struct HotkeyConfigurations {
  Hotkey move_space_left;
  Hotkey move_space_right;

  template <typename Sink>
  friend void AbslStringify(Sink &sink,
                            const HotkeyConfigurations &hotkey_configs) {
    absl::Format(
        &sink, "HotkeyConfigurations{move_space_left=%s, move_space_right=%s}",
        absl::StrCat(hotkey_configs.move_space_left),
        absl::StrCat(hotkey_configs.move_space_right));
  }
};

absl::StatusOr<HotkeyConfigurations> LoadHotkeyConfiguration();

} // namespace fasterswiper
