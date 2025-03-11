#include "core.h"

#include <vector>
#include <iostream>

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

void Core::Update(float delta_time) {
  for (auto& [_, world_ptr] : worlds_) {
    world_ptr->Update(delta_time);
  }
}

void Core::CleanupWorlds() {
  worlds_to_remove_.clear();

  for (auto& [_, world_ptr] : worlds_) {
    if (world_ptr->IsPendindDestrys()) {
      worlds_to_remove_.push_back(world_ptr->GetId());
    }
  }

  for (auto world_id : worlds_to_remove_) {
    worlds_.erase(world_id);
  }
}

int Core::Run() {
  if (config_.NeedShowHelp()) {
    std::cout << config_ << "\n";

    return 0;
  }
  return 0;
}

}  // namespace the
