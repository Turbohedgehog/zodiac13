#include "core.h"

#include <vector>
#include <iostream>
#include <chrono>
#include <limits>

#include "module_base.h"
#include "world.h"

namespace the {

Core::Core(int argc, char *argv[]) {
  config_.ParseCommandLineArguments(argc, argv);
}

const Config& Core::GetConfig() const {
  return config_;
}

bool Core::AddModule(ModulePtr module) {
  return modules_.try_emplace(module->GetName(), module).second;
}

void Core::InitModules() {
  for (auto& [_, module_ptr] : modules_) {
    module_ptr->Init(*this);
  }
}

WorldWeakPtr Core::CreateWorld() {
  auto world = std::make_shared<World>(new_world_id_);
  for (auto& [_, module_ptr] : modules_) {
    module_ptr->OnWorldCreated(world);
  }
  worlds_[new_world_id_] = world;
  ++new_world_id_;

  return world;
}

WorldWeakPtr Core::GetWorld(WorldId world_id) const {
  auto world_it = worlds_.find(world_id);
  return world_it != worlds_.end() ? world_it->second : WorldWeakPtr();
}

void Core::Update(double delta_time) {
  for (auto& [_, world_ptr] : worlds_) {
    world_ptr->Update(delta_time);
  }
}

void Core::CleanupWorlds() {
  worlds_to_remove_.clear();

  for (auto& [_, world_ptr] : worlds_) {
    if (world_ptr->IsPendindDestroy() || world_ptr->Empty()) {
      worlds_to_remove_.push_back(world_ptr->GetId());
    }
  }

  for (auto world_id : worlds_to_remove_) {
    worlds_.erase(world_id);
  }
}

bool Core::IsPendingShutDown() const {
  return pending_shutdown_;
}

int Core::Run() {
  if (config_.NeedShowHelp()) {
    std::cout << config_ << "\n";

    return 0;
  }

  if (config_.GetFPS() <= std::numeric_limits<double>::epsilon()) {
    return 1;
  }

  const auto update_time = 1. / config_.GetFPS();
  std::chrono::duration<double> sleep_time(update_time), frame_delta(update_time);
  auto accumulated_frame_time = std::chrono::duration<double>::zero();
  auto sleep_duration = sleep_time;
  auto prev = std::chrono::high_resolution_clock::now();
  while (!worlds_.empty() && !IsPendingShutDown()) {
    if (sleep_duration > std::chrono::duration<double>::zero()) {
      std::this_thread::sleep_for(sleep_duration);
    }

    Update(update_time);
    CleanupWorlds();
    auto now = std::chrono::high_resolution_clock::now();
    accumulated_frame_time += now - prev - sleep_duration;
    sleep_duration = std::max(frame_delta - accumulated_frame_time, std::chrono::duration<double>::zero());
    accumulated_frame_time = std::min(frame_delta - sleep_duration, std::chrono::duration<double>::zero());
    prev = now;
  }

  return 0;
}

}  // namespace the
