#include "src/engine/position-reporter.h"
#include "src/space-state.h"

#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "gutil/status.h"

namespace fasterswiper {

absl::Status Run() {
  {
    ASSIGN_OR_RETURN(SpaceState space_state, LoadSpaceStateForActiveDisplay());
    HorizontalAxisAdapter reporter(std::move(space_state));
    const auto [min, max] = reporter.position_soft_limits();
    std::cout << "Horizontal position: committed_position="
              << reporter.committed_position() << ", position_soft_limits={"
              << min << ", " << max << "}\n";
  }
  {
    VerticalAxisAdapter reporter;
    const auto [vertical_min, vertical_max] = reporter.position_soft_limits();
    std::cout << "Vertical position: committed_position="
              << reporter.committed_position() << ", position_soft_limits={"
              << vertical_min << ", " << vertical_max << "}\n";
  }

  return absl::OkStatus();
}

} // namespace fasterswiper

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  QCHECK_OK(::fasterswiper::Run());
  return 0;
}
