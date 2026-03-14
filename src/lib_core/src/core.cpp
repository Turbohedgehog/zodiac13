#include <lib_core/core.h>

#include <vector>
#include <iostream>
#include <chrono>
#include <limits>
#include <thread>
#include <typeinfo>

#include <lib_core/components.h>
#include <lib_core/module_factory_base.h>

#include "module_lib_holder.h"

namespace z13 {

Core::Core(int argc, char *argv[])
  : module_lib_holder_(std::make_unique<ModuleLibHolder>()) {
  // : module_lib_holder_(std::make_shared<ModuleLibHolder>()) {
  config_.ParseCommandLineArguments(argc, argv);
}

Core::~Core() = default;

const Config& Core::GetConfig() const {
  return config_;
}

bool Core::RegisterModuleFactory(ModuleFactoryPtr module_factory) {
  return !!module_factories_.emplace_back(module_factory);
  // return true;
  // return module_factories_.insert({module_factory->GetName(), module_factory}).second;
}

bool Core::RegisterModuleFactory(const std::filesystem::path& module_lib_path, bool append_platform_extension) {
  auto module_factory = module_lib_holder_->AppendModuleLib(module_lib_path, append_platform_extension);
  if (!module_factory) {
    return false;
  }

  return RegisterModuleFactory(module_factory);
}

WorldRef Core::CreateWorld() {
  auto it = worlds_.insert({new_world_id_, flecs::world()});
  ++new_world_id_;

  auto world_ref = WorldRef(it.first->second);
  world_ref.get().component<CoreComponent>();
  CoreComponent core_component {.core = *this};
  world_ref.get().set(core_component);
  // for (auto [_, module_factory_ptr] : module_factories_) {
  for (auto& module_factory_ptr : module_factories_) {
    module_factory_ptr->RegisterModules(world_ref);
  }

  return world_ref;
}

void Core::Update(float delta_time) {
  for (auto& [_, world] : worlds_) {
    world.progress(delta_time);
  }
}

void Core::Shutdown() {
  pending_shutdown_ = true;
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
    // CleanupWorlds();
    auto now = std::chrono::high_resolution_clock::now();
    accumulated_frame_time += now - prev - sleep_duration;
    sleep_duration = std::max(frame_delta - accumulated_frame_time, std::chrono::duration<double>::zero());
    accumulated_frame_time = std::min(frame_delta - sleep_duration, std::chrono::duration<double>::zero());
    prev = now;
  }

  return 0;
}

}  // namespace z13
