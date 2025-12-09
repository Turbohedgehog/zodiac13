#pragma once

#include <atomic>
#include <thread>
#include <memory>

#include <z13_module/components/input.h>

namespace z13::gameplay {

struct GameplayInputSystemTag {};

struct LoadingInputSystemStateRaw {
  std::atomic<bool> is_complete = false;
  std::thread load_config_thread;
  z13::input::InputConfig input_config;
};

struct LoadingInputSystemState {
  std::shared_ptr<LoadingInputSystemStateRaw> input_state;
};

}  // namespace z13::gameplay
