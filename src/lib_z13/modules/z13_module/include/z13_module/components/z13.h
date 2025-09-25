#pragma once

#include <string>

// #include <eigen3/Eigen/Dense>

// #include <flecs.h>

namespace z13 {

struct Z13State {
  bool shutdown = false;
};

struct PlayerInfoComponent {
  uint32_t id = 0;
  std::string login;
  std::string name;
};

}  // namespace z13
