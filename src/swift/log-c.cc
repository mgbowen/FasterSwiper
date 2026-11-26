#include "src/swift/log-c.h"

#include "src/swift/log.pb.h"

#include <cstddef>
#include <cstdint>

#include <absl/base/log_severity.h>
#include <absl/log/log.h>
#include <absl/log/vlog_is_on.h>
#include <absl/strings/string_view.h>

namespace {

absl::string_view BytesToStringView(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return {};
  }
  return absl::string_view(reinterpret_cast<const char*>(data), len);
}

absl::LogSeverity ToAbslSeverity(int32_t raw_severity) {
  if (!fasterswiper::swift::LogSeverity_IsValid(raw_severity)) {
    return absl::LogSeverity::kError;
  }
  switch (static_cast<fasterswiper::swift::LogSeverity>(raw_severity)) {
    case fasterswiper::swift::LOG_SEVERITY_INFO:
      return absl::LogSeverity::kInfo;
    case fasterswiper::swift::LOG_SEVERITY_WARNING:
      return absl::LogSeverity::kWarning;
    case fasterswiper::swift::LOG_SEVERITY_ERROR:
      return absl::LogSeverity::kError;
    case fasterswiper::swift::LOG_SEVERITY_FATAL:
      return absl::LogSeverity::kFatal;
  }
  return absl::LogSeverity::kError;
}

}  // namespace

extern "C" {

bool FS_VLogIsOn(int32_t level, const uint8_t* file, size_t file_len) {
  const absl::string_view file_view = BytesToStringView(file, file_len);
  return level <= absl::log_internal::VLogLevel(file_view);
}

void FS_LogMessage(int32_t severity, int32_t verbosity, const uint8_t* file,
                   size_t file_len, int32_t line, const uint8_t* message,
                   size_t message_len) {
  const absl::string_view file_view = BytesToStringView(file, file_len);
  const absl::string_view message_view =
      BytesToStringView(message, message_len);
  const absl::LogSeverity absl_severity = ToAbslSeverity(severity);

  if (verbosity >= 0) {
    LOG(LEVEL(absl_severity))
            .AtLocation(file_view, line)
            .WithVerbosity(verbosity)
        << message_view;
  } else {
    LOG(LEVEL(absl_severity)).AtLocation(file_view, line) << message_view;
  }
}

}  // extern "C"
