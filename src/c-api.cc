#include "src/c-api.h"

#include "src/cf-util.h"
#include "src/event-tap-manager.h"
#include "src/macos-private.h"
#include "src/physical-event-handler.h"
#include "src/version.h"

#include <CoreGraphics/CGEventTypes.h>
#include <cstring>
#include <iostream>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOTypes.h>

#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace {

using ::fasterswiper::CFUniquePtr;
using ::fasterswiper::EventTapManager;
using ::fasterswiper::kCGSEventDockControl;
using ::fasterswiper::PhysicalEventHandler;
using ::fasterswiper::WrapCFUnique;

} // namespace

extern "C" {

struct FasterSwiper {
  absl::Mutex mutex;

  std::shared_ptr<PhysicalEventHandler> physical_event_handler;
  std::unique_ptr<EventTapManager> tap_manager;
  CFUniquePtr<CFRunLoopSourceRef> run_loop_source;

  bool is_running = false;
};

void InitializeFasterSwiperOptions(FasterSwiperOptions *options) {
  std::memset(options, 0, sizeof(FasterSwiperOptions));

  const PhysicalEventHandler::Options default_options;
  options->animation_duration_per_space_ns =
      absl::ToInt64Nanoseconds(default_options.animation_duration_per_space);
  options->easing_function_type = default_options.easing_function_type;
  options->ticks_per_second = default_options.ticks_per_second;
  options->handle_keyboard_events = default_options.handle_keyboard_events;
}

FasterSwiper *CreateFasterSwiper(const FasterSwiperOptions *options) {
  absl::StatusOr<std::shared_ptr<PhysicalEventHandler>>
      maybe_physical_event_handler =
          PhysicalEventHandler::Create(PhysicalEventHandler::Options{
              .animation_duration_per_space =
                  absl::Nanoseconds(options->animation_duration_per_space_ns),
              .easing_function_type = options->easing_function_type,
              .ticks_per_second = options->ticks_per_second,
              .handle_keyboard_events = options->handle_keyboard_events,
          });

  if (!maybe_physical_event_handler.ok()) {
    std::cerr << "Failed to create PhysicalEventHandler: "
              << maybe_physical_event_handler.status();
    return nullptr;
  }

  std::shared_ptr<PhysicalEventHandler> physical_event_handler =
      *std::move(maybe_physical_event_handler);

  EventTapManager::Callback callback =
      [physical_event_handler](CGEventTapProxy proxy, CGEventType event_type,
                               CGEventRef event) -> CGEventRef {
    return physical_event_handler->HandleEvent(proxy, event_type, event);
  };

  std::vector<CGEventType> event_types;
  event_types.push_back(kCGSEventDockControl);

  if (options->handle_keyboard_events) {
    event_types.push_back(kCGEventKeyDown);
  }

  auto maybe_tap_manager = EventTapManager::Create(
      kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
      event_types, std::move(callback));
  if (!maybe_tap_manager.ok()) {
    std::cerr << "Failed to create EventTapManager: "
              << maybe_tap_manager.status() << "\n";
    return nullptr;
  }

  std::unique_ptr<EventTapManager> tap_manager = std::move(*maybe_tap_manager);

  CFUniquePtr<CFRunLoopSourceRef> run_loop_source =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));

  return new FasterSwiper{
      .physical_event_handler = std::move(physical_event_handler),
      .tap_manager = std::move(tap_manager),
      .run_loop_source = std::move(run_loop_source),
  };
}

bool DestroyFasterSwiper(FasterSwiper *state) {
  if (state == nullptr) {
    return false;
  }

  delete state;
  return true;
}

bool StartFasterSwiper(FasterSwiper *state) {
  if (state == nullptr) {
    std::cerr << "StartFasterSwiper called with null state\n";
    return false;
  }

  absl::MutexLock lock(state->mutex);

  if (state->is_running) {
    std::cerr << "StartFasterSwiper called with already running state\n";
    return false;
  }

  CFRunLoopAddSource(CFRunLoopGetMain(), state->run_loop_source.get(),
                     kCFRunLoopCommonModes);
  state->tap_manager->SetEnabled(true);
  state->is_running = true;

  return true;
}

bool StopFasterSwiper(FasterSwiper *state) {
  if (state == nullptr) {
    std::cerr << "StopFasterSwiper called with null state\n";
    return false;
  }

  absl::MutexLock lock(state->mutex);

  if (!state->is_running) {
    return true;
  }

  state->tap_manager->SetEnabled(false);
  CFRunLoopRemoveSource(CFRunLoopGetMain(), state->run_loop_source.get(),
                        kCFRunLoopCommonModes);
  state->is_running = false;

  return true;
}

void ParseFasterSwiperCommandLine(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
}

void GetFasterSwiperVersionInfo(FasterSwiperVersionInfo *info) {
  if (info == nullptr) {
    return;
  }

  info->version =
      fasterswiper::kVersion ? fasterswiper::kVersion->data() : nullptr;
  info->git_hash = fasterswiper::kGitCommitHash.data();
  info->is_dirty = fasterswiper::kGitIsDirty;
}
}
