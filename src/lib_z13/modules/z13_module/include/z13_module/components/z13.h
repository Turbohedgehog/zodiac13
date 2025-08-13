#pragma once

#include <string>

#include <flecs.h>

namespace z13 {

struct Z13State {
  bool shutdown = false;
};

struct MainMenuPipelineComponent {
  flecs::entity pipeline;
  flecs::entity update_data_phase;
  flecs::entity draw_phase;
};

struct PlayerInfoComponent {
  uint32_t id = 0;
  std::string login;
  std::string name;
};

}  // namespace z13
