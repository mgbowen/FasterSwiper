#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true if VLOG(level) is enabled for `file` under Abseil's current
// --v and --vmodule configuration.
bool FS_VLogIsOn(int32_t level, const uint8_t* file, size_t file_len);

// Emits a log entry through Abseil's logging pipeline attributed to
// `file`:`line`. `severity` is a `fasterswiper.swift.LogSeverity` protobuf enum
// value. Pass a negative `verbosity` for non-VLOG messages.
void FS_LogMessage(int32_t severity, int32_t verbosity, const uint8_t* file,
                   size_t file_len, int32_t line, const uint8_t* message,
                   size_t message_len);

#ifdef __cplusplus
}
#endif
