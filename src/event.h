#pragma once

#include <optional>
#include <string>

#include <ApplicationServices/ApplicationServices.h>

namespace fasterswiper {

constexpr int64_t kSyntheticEventMagicNumber = 0x737769706374726C;

enum class DockSwipeEventSource {
  kPhysical,
  kSynthetic,
};

struct DockSwipeEvent {
  int phase;
  int direction;
  double progress;
  DockSwipeEventSource source;
};

std::optional<DockSwipeEvent> ParseDockSwipeEvent(CGEventRef event);

std::string EventGesturePhaseToString(int phase);

std::string CFEventToDebugString(CGEventRef event);

} // namespace fasterswiper
