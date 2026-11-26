#include <cstdint>

#include <absl/base/log_severity.h>
#include <absl/log/globals.h>
#include <absl/log/scoped_mock_log.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" {
void fs_swift_log_test_emit_severities();
void fs_swift_log_test_vlog(int32_t level, int32_t* eval_count);
bool fs_swift_log_test_vlog_is_on(int32_t level);
void fs_swift_log_test_fatal();
}  // extern "C"

namespace fasterswiper {
namespace {

using ::testing::EndsWith;

TEST(SwiftLogTest, EmitsSeveritiesWithSwiftFileAttribution) {
  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo,
                       EndsWith("LogTestHelper.swift"), "swift info message"));
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kWarning, EndsWith("LogTestHelper.swift"),
                  "swift warning message"));
  EXPECT_CALL(log, Log(absl::LogSeverity::kError,
                       EndsWith("LogTestHelper.swift"), "swift error message"));

  log.StartCapturingLogs();
  fs_swift_log_test_emit_severities();
}

TEST(SwiftLogTest, VlogRespectsGlobalAndModuleLevelsAndIsLazy) {
  const int prev_global = absl::SetGlobalVLogLevel(0);
  const int prev_module = absl::SetVLogLevel("LogTestHelper", 0);

  int32_t eval_count = 0;
  EXPECT_FALSE(fs_swift_log_test_vlog_is_on(1));
  {
    absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);
    log.StartCapturingLogs();
    fs_swift_log_test_vlog(1, &eval_count);
  }
  EXPECT_EQ(eval_count, 0);

  // Per-file --vmodule matching strips the ".swift" suffix.
  absl::SetVLogLevel("LogTestHelper", 2);
  EXPECT_TRUE(fs_swift_log_test_vlog_is_on(1));
  EXPECT_TRUE(fs_swift_log_test_vlog_is_on(2));
  EXPECT_FALSE(fs_swift_log_test_vlog_is_on(3));
  {
    absl::ScopedMockLog log(absl::MockLogDefault::kDisallowUnexpected);
    EXPECT_CALL(log,
                Log(absl::LogSeverity::kInfo, EndsWith("LogTestHelper.swift"),
                    "swift vlog level 2"));
    log.StartCapturingLogs();
    fs_swift_log_test_vlog(2, &eval_count);
    fs_swift_log_test_vlog(3, &eval_count);
  }
  EXPECT_EQ(eval_count, 1);

  // Restore previous levels.
  absl::SetVLogLevel("LogTestHelper", prev_module);
  absl::SetGlobalVLogLevel(prev_global);
}

TEST(SwiftLogDeathTest, FatalLogsMessageAndAborts) {
  EXPECT_DEATH(fs_swift_log_test_fatal(),
               "LogTestHelper\\.swift.*swift fatal message");
}

}  // namespace
}  // namespace fasterswiper
