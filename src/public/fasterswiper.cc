#include "src/public/fasterswiper.h"

#include "src/cf-util.h"
#include "src/compatibility.h"
#include "src/easing.h"
#include "src/engine/physical-event-handler.h"
#include "src/event-tap-manager.h"
#include "src/macos-private.h"
#include "src/proto-util.h"
#include "src/public/fasterswiper.pb.h"
#include "src/version.h"

#include <cstring>
#include <iostream>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGEventTypes.h>
#include <IOKit/IOTypes.h>
#include <absl/debugging/failure_signal_handler.h>
#include <absl/debugging/symbolize.h>
#include <absl/flags/parse.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/status/statusor.h>

namespace {

using ::fasterswiper::CFUniquePtr;
using ::fasterswiper::EasingFunction;
using ::fasterswiper::EventTapManager;
using ::fasterswiper::kCGSEventDockControl;
using ::fasterswiper::PhysicalEventHandler;
using ::fasterswiper::ToProtoDuration;
using ::fasterswiper::WrapCFUnique;

namespace proto = fasterswiper::proto;

proto::DaemonOptions GetDefaultDaemonOptions() {
  proto::DaemonOptions options;

  auto* horizontal = options.mutable_horizontal_settings();
  horizontal->set_enabled(true);
  *horizontal->mutable_duration() = ToProtoDuration(absl::Milliseconds(200));
  horizontal->set_easing_function(proto::EASING_FUNCTION_QUADRATIC_EASE_OUT);

  auto* vertical = options.mutable_vertical_settings();
  vertical->set_enabled(true);
  *vertical->mutable_duration() = ToProtoDuration(absl::Milliseconds(200));
  vertical->set_easing_function(proto::EASING_FUNCTION_QUINTIC_EASE_OUT);

  options.set_frames_per_second(240);
  options.set_intercept_mission_control_shortcuts(true);
  options.set_enable_jump_to_space_shortcuts(true);
  return options;
}

}  // namespace

extern "C" {

void FS_Init(int argc, char** argv) {
  static std::once_flag init_flag;
  std::call_once(init_flag, [&] {
    std::vector<char*> positional_args;
    std::vector<absl::UnrecognizedFlag> unrecognized_flags;
    absl::ParseAbseilFlagsOnly(argc, argv, positional_args, unrecognized_flags);

    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

    absl::InitializeSymbolizer(argv[0]);
    absl::InstallFailureSignalHandler(absl::FailureSignalHandlerOptions());

    LOG(INFO) << "FasterSwiper initialized";
  });
}

struct FS_Daemon {
  absl::Mutex mutex;

  std::unique_ptr<FS_DaemonOptions> options;
  std::shared_ptr<PhysicalEventHandler> physical_event_handler;
  std::unique_ptr<EventTapManager> tap_manager;
  CFUniquePtr<CFRunLoopSourceRef> run_loop_source;

  std::unique_ptr<std::thread> interceptor;

  bool is_running = false;
};

struct FS_DaemonOptions {
  proto::DaemonOptions options;
};

bool FS_LoadDefaultDaemonOptions(FS_DaemonOptions** out_daemon_options) {
  if (out_daemon_options == nullptr) {
    return false;
  }

  auto daemon_options = new FS_DaemonOptions;
  daemon_options->options = GetDefaultDaemonOptions();
  *out_daemon_options = daemon_options;
  return true;
}

bool FS_HydrateDaemonOptions(FS_DaemonOptions* daemon_options) {
  proto::DaemonOptions default_options = GetDefaultDaemonOptions();

  // 1. Migrate legacy fields if present.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  const bool has_legacy_duration =
      daemon_options->options.has_animation_duration_per_space();
  const bool has_legacy_easing = daemon_options->options.has_easing_function();
  const bool has_legacy_bezier =
      daemon_options->options.has_cubic_bezier_curve();

  if (has_legacy_duration || has_legacy_easing || has_legacy_bezier) {
    const google::protobuf::Duration legacy_default_duration =
        ToProtoDuration(absl::Milliseconds(200));

    auto* horizontal = daemon_options->options.mutable_horizontal_settings();
    auto* vertical = daemon_options->options.mutable_vertical_settings();

    if (has_legacy_duration) {
      const auto& legacy_duration =
          daemon_options->options.animation_duration_per_space();
      if (legacy_duration.seconds() != legacy_default_duration.seconds() ||
          legacy_duration.nanos() != legacy_default_duration.nanos()) {
        *horizontal->mutable_duration() = legacy_duration;
        *vertical->mutable_duration() = legacy_duration;
      }
      daemon_options->options.clear_animation_duration_per_space();
    }

    if (has_legacy_easing) {
      auto legacy_easing = daemon_options->options.easing_function();
      if (legacy_easing != proto::EASING_FUNCTION_QUADRATIC_EASE_OUT) {
        horizontal->set_easing_function(legacy_easing);
        vertical->set_easing_function(legacy_easing);
      }
      daemon_options->options.clear_easing_function();
    }

    if (has_legacy_bezier) {
      *horizontal->mutable_cubic_bezier_curve() =
          daemon_options->options.cubic_bezier_curve();
      *vertical->mutable_cubic_bezier_curve() =
          daemon_options->options.cubic_bezier_curve();
      daemon_options->options.clear_cubic_bezier_curve();
    }
  }
#pragma clang diagnostic pop

  // Clear legacy fields from unknown fields if present.
  daemon_options->options.mutable_unknown_fields()->DeleteByNumber(1);
  daemon_options->options.mutable_unknown_fields()->DeleteByNumber(2);
  daemon_options->options.mutable_unknown_fields()->DeleteByNumber(3);

  // 2. Hydrate missing fields on horizontal_settings.
  auto* horizontal = daemon_options->options.mutable_horizontal_settings();
  if (!horizontal->has_enabled()) {
    horizontal->set_enabled(true);
  }
  if (!horizontal->has_duration()) {
    *horizontal->mutable_duration() = ToProtoDuration(absl::Milliseconds(200));
  }
  if (!horizontal->has_easing_function()) {
    horizontal->set_easing_function(proto::EASING_FUNCTION_QUADRATIC_EASE_OUT);
  }

  // 3. Hydrate missing fields on vertical_settings.
  auto* vertical = daemon_options->options.mutable_vertical_settings();
  if (!vertical->has_enabled()) {
    vertical->set_enabled(true);
  }
  if (!vertical->has_duration()) {
    *vertical->mutable_duration() = ToProtoDuration(absl::Milliseconds(200));
  }
  if (!vertical->has_easing_function()) {
    vertical->set_easing_function(proto::EASING_FUNCTION_QUINTIC_EASE_OUT);
  }

  if (!daemon_options->options.has_frames_per_second()) {
    daemon_options->options.set_frames_per_second(
        default_options.frames_per_second());
  }

  if (!daemon_options->options.has_intercept_mission_control_shortcuts()) {
    daemon_options->options.set_intercept_mission_control_shortcuts(
        default_options.intercept_mission_control_shortcuts());
  }

  if (!daemon_options->options.has_enable_jump_to_space_shortcuts()) {
    daemon_options->options.set_enable_jump_to_space_shortcuts(
        default_options.enable_jump_to_space_shortcuts());
  }

  return true;
}

bool FS_LoadDaemonOptionsFromBinaryProto(
    const char* data, size_t len, FS_DaemonOptions** out_daemon_options_ptr) {
  auto binary_proto_data = absl::string_view(data, len);
  auto options = std::make_unique<FS_DaemonOptions>();
  if (!options->options.ParseFromString(binary_proto_data)) {
    LOG(ERROR) << "proto::DaemonOptions.ParseFromString() failed";
    return false;
  }

  *out_daemon_options_ptr = options.release();
  return true;
}

bool FS_SaveDaemonOptionsToBinaryProto(const FS_DaemonOptions* daemon_options,
                                       char* out_data, size_t* out_len) {
  if (daemon_options == nullptr) {
    return false;
  }

  if (out_len == nullptr) {
    return false;
  }

  std::string binary_proto = daemon_options->options.SerializeAsString();

  if (out_data != nullptr) {
    if (*out_len < binary_proto.size()) {
      return false;
    }

    // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
    std::memcpy(out_data, binary_proto.data(), binary_proto.size());
  }

  *out_len = binary_proto.size();
  return true;
}

FS_Daemon* FS_Create(FS_DaemonOptions* options) {
  absl::StatusOr<std::shared_ptr<PhysicalEventHandler>>
      maybe_physical_event_handler =
          PhysicalEventHandler::Create(options->options);

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

  const bool horizontal_enabled =
      options->options.has_horizontal_settings()
          ? options->options.horizontal_settings().enabled()
          : true;
  const bool jump_to_space_enabled =
      options->options.enable_jump_to_space_shortcuts() && horizontal_enabled;

  if (options->options.intercept_mission_control_shortcuts() ||
      jump_to_space_enabled) {
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

  return new FS_Daemon{
      .options = absl::WrapUnique(options),
      .physical_event_handler = std::move(physical_event_handler),
      .tap_manager = std::move(tap_manager),
      .run_loop_source = std::move(run_loop_source),
  };
}

bool FS_Destroy(FS_Daemon* state) {
  if (state == nullptr) {
    return false;
  }

  delete state;
  return true;
}

bool FS_DestroyDaemonOptions(FS_DaemonOptions* options) {
  if (options == nullptr) {
    return false;
  }

  delete options;
  return true;
}

bool FS_Start(FS_Daemon* state) {
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

bool FS_Stop(FS_Daemon* state) {
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

void FS_GetVersionInfo(FS_VersionInfo* info) {
  if (info == nullptr) {
    return;
  }

  info->version =
      fasterswiper::kVersion ? fasterswiper::kVersion->data() : nullptr;
  info->git_hash = fasterswiper::kGitCommitHash.data();
  info->is_dirty = fasterswiper::kGitIsDirty;
}
}
