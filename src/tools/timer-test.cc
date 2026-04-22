#include "src/periodic-timer.h"

#include <iostream>

int main() {
  uint64_t last_tick = 0;
  int i = 0;
  fasterswiper::PeriodicTimer timer(fasterswiper::PeriodicTimer::Parameters{
      .period_ns = 1000000000 / 240,
      .tick_callback =
          [&](uint64_t time_since_start_ns) {
            if (last_tick == 0) {
              last_tick = time_since_start_ns;
              return fasterswiper::PeriodicTimerTickResult::kContinueTimer;
            }

            int64_t elapsed_ns = time_since_start_ns - last_tick;
            std::cout << std::format("[{}] Timer fired: {:.2f} ms\n", i,
                                     elapsed_ns / 1000000.0);
            last_tick = time_since_start_ns;
            ++i;
            return fasterswiper::PeriodicTimerTickResult::kContinueTimer;
          },
      .stopped_callback =
          [](fasterswiper::PeriodicTimerStopReason stop_reason) {
            std::string reason_string;

            std::cout << "Timer stopped for reason \"" << reason_string
                      << "\"\n";
          },
  });

  std::string asd;
  std::cin >> asd;

  return 0;
}
