#pragma once

namespace fasterswiper {

template <typename... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

} // namespace fasterswiper
