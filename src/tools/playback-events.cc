#include "src/cf-util.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"
#include "src/tools/util/accessibility-check.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/log/check.h>
#include <absl/status/status.h>
#include <absl/strings/escaping.h>
#include <absl/strings/str_cat.h>
#include <nlohmann/json.hpp>

ABSL_FLAG(double, playback_speed, 1.0,
          "Multiplier on playback speed. e.g., 0.5 is half speed (double duration), "
          "2.0 is double speed (half duration).");

namespace fasterswiper {
namespace {

using nlohmann::json;

void PostEvent(const json &j) {
  std::string buffer;
  CHECK(absl::Base64Unescape((std::string)j.at("data"), &buffer));

  auto cf_buffer = WrapCFUnique(CFDataCreateWithBytesNoCopy(
      nullptr, reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      kCFAllocatorNull));
  auto dock = WrapCFUnique(CGEventCreateFromData(nullptr, cf_buffer.get()));
  CHECK(dock != nullptr);

  CGEventPost(kCGSessionEventTap, dock.get());
}

absl::Status Run(const std::string &input_path, double playback_speed) {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  std::ifstream in(input_path);
  if (!in.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Could not open file for reading: ", input_path));
  }

  json root = json::parse(in, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded()) {
    return absl::InvalidArgumentError("Failed to parse JSON");
  }

  if (!root.contains("events") || !root["events"].is_array()) {
    return absl::InvalidArgumentError("JSON missing 'events' array.");
  }

  auto events = root["events"];
  if (events.empty()) {
    std::cout << "No events to playback.\n";
    return absl::OkStatus();
  }

  std::cout << "Starting playback of " << events.size() << " events...\n";

  const int64_t playback_start_ns = UptimeInNanoseconds();
  int64_t cumulative_delta_ns = 0;

  for (const auto &j : events) {
    const int64_t delta_ns = j["delta_ns"];
    cumulative_delta_ns += static_cast<int64_t>(std::round(delta_ns / playback_speed));

    const int64_t now_ns = UptimeInNanoseconds();
    auto target_ns = playback_start_ns + cumulative_delta_ns;

    if (target_ns > now_ns) {
      std::this_thread::sleep_for(std::chrono::nanoseconds(target_ns - now_ns));
    }

    PostEvent(j);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "Playback completed.\n";
  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main(int argc, char **argv) {
  std::vector<char*> positional_args = absl::ParseCommandLine(argc, argv);
  if (positional_args.size() != 2) {
    std::cerr << "Usage: " << positional_args[0] << " <input_json_path> [--playback_speed <multiplier>]\n";
    return 1;
  }

  std::string input_path = positional_args[1];
  double playback_speed = absl::GetFlag(FLAGS_playback_speed);
  if (playback_speed <= 0.0) {
    std::cerr << "Error: --playback_speed must be positive.\n";
    return 1;
  }

  QCHECK_OK(::fasterswiper::Run(input_path, playback_speed));
  return 0;
}
