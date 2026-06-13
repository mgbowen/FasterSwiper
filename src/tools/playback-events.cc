#include "src/cf-util.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"
#include "src/tools/util/accessibility-check.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include <absl/log/check.h>
#include <absl/status/status.h>
#include <absl/strings/escaping.h>
#include <absl/strings/str_cat.h>
#include <nlohmann/json.hpp>

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

absl::Status Run(const std::string &input_path) {
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
    cumulative_delta_ns += delta_ns;

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
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <input_json_path>\n";
    return 1;
  }

  std::string input_path = argv[1];
  QCHECK_OK(::fasterswiper::Run(input_path));
  return 0;
}
