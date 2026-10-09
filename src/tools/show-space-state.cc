#include "src/cf-collections-util.h"
#include "src/macos-private.h"
#include "src/mission-control.h"
#include "src/space-state.h"

#include <iostream>

#include <absl/flags/parse.h>
#include <absl/strings/str_cat.h>
#include <magic_enum/magic_enum.hpp>

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  auto space_state = fasterswiper::LoadSpaceStateForActiveDisplay();
  if (!space_state.ok()) {
    std::cerr << "Failed to load space state for display: "
              << space_state.status().message() << "\n";
    return 1;
  }
  std::cout << absl::StrCat(*space_state) << "\n";

  const int cid = SLSMainConnectionID();
  auto spaces_ref = fasterswiper::WrapCFUnique(
      SLSCopySpaces(cid, CGSSpaceMask::kCGSCurrentSpacesMask));
  const CFIndex spaces_length = CFArrayGetCount(spaces_ref.get());

  std::vector<SLSSpaceId> space_ids;
  for (CFIndex i = 0; i < spaces_length; i++) {
    auto space_id =
        *fasterswiper::CFArrayGetAs<SLSSpaceId>(spaces_ref.get(), i);
    space_ids.push_back(space_id);
  }

  std::cout << "Current space IDs: [" << absl::StrJoin(space_ids, ", ")
            << "]\n";

  absl::StatusOr<fasterswiper::ActiveMultitaskingWindow> maybe_window =
      fasterswiper::GetActiveMultitaskingWindow();
  std::cout << "Active multitasking window: "
            << magic_enum::enum_name(*maybe_window) << "\n";

  return 0;
}
