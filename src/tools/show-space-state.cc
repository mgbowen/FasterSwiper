#include "src/space-state.h"

#include <iostream>

#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);

  auto space_state = fasterswiper::LoadSpaceStateForActiveDisplay();
  if (!space_state.ok()) {
    std::cerr << "Failed to load space state for display: "
              << space_state.status().message() << "\n";
    return 1;
  }
  std::cout << absl::StrCat(*space_state) << "\n";

  return 0;
}
