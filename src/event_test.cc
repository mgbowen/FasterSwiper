#include "src/event.h"

#include <array>
#include <atomic>
#include <cstdint>

#include <gtest/gtest.h>
#include <mach/mach_time.h>

namespace fasterswiper {
namespace {

TEST(EventTest, MonotonicTimestampConversionAcrossTimebases) {
  constexpr std::array<mach_timebase_info_data_t, 4> kTimebases = {{
      {.numer = 125, .denom = 3},  // Apple Silicon (24 MHz)
      {.numer = 1, .denom = 1},    // Intel (1 GHz)
      {.numer = 1, .denom = 2},    // numer < denom
      {.numer = 2, .denom = 5},    // non-integer numer < denom
  }};

  for (const auto& tb : kTimebases) {
    SCOPED_TRACE(::testing::Message()
                 << "numer=" << tb.numer << " denom=" << tb.denom);
    std::atomic<uint64_t> last_ticks{0};
    constexpr uint64_t kFrozenNow = 10'000'000ULL;

    uint64_t prev_ticks = 0;
    uint64_t prev_ns = 0;
    uint64_t prev_roundtrip_ticks = 0;

    for (int i = 0; i < 16; ++i) {
      const uint64_t ticks =
          AdvanceMonotonicMachTimestamp(kFrozenNow, tb, last_ticks);
      const uint64_t ns = MachTicksToNanoseconds(ticks, tb);
      const uint64_t roundtrip_ticks = NanosecondsToMachTicks(ns, tb);

      EXPECT_GT(ticks, prev_ticks) << "i=" << i;
      EXPECT_GT(ns, prev_ns) << "i=" << i;
      EXPECT_GT(roundtrip_ticks, prev_roundtrip_ticks) << "i=" << i;
      if (tb.numer >= tb.denom) {
        EXPECT_EQ(roundtrip_ticks, ticks) << "i=" << i;
      }

      prev_ticks = ticks;
      prev_ns = ns;
      prev_roundtrip_ticks = roundtrip_ticks;
    }
  }
}

}  // namespace
}  // namespace fasterswiper
