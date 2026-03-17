#pragma once

#include <string>

namespace z13 {

struct PlayerInfoComponent {
  uint32_t id = 0;
  std::string login;
  std::string name;
};

}  // namespace z13
