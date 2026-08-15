// z13.core.core module implementation unit.
// Определяет CoreImpl (PIMPL), скрывающий flecs::world и Config из
// экспортируемого интерфейса модуля z13.core.core.

module;

#include <vector>
#include <iostream>
#include <chrono>
#include <limits>
#include <thread>
#include <typeinfo>
#include <filesystem>
#include <map>
#include <memory>

#include <flecs.h>
#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>

#include "module_lib_holder.h"

module z13.core.core;

import z13.core.config;
import z13.core.components;

namespace z13 {

struct CoreImpl {
  Config config_;
  std::map<WorldId, flecs::world> worlds_;
  WorldId new_world_id_ = 0;
  std::vector<ModuleFactoryPtr> module_factories_;
  bool pending_shutdown_ = false;
  std::unique_ptr<ModuleLibHolder> module_lib_holder_;
};

Core::Core(int argc, char* argv[])
  : impl_(std::make_unique<CoreImpl>()) {
  impl_->module_lib_holder_ = std::make_unique<ModuleLibHolder>();
  impl_->config_.ParseCommandLineArguments(argc, argv);
}

Core::~Core() = default;

const Config& Core::GetConfig() const {
  return impl_->config_;
}

bool Core::RegisterModuleFactory(ModuleFactoryPtr module_factory) {
  return !!impl_->module_factories_.emplace_back(std::move(module_factory));
}

bool Core::RegisterModuleFactory(const std::filesystem::path& module_lib_path, bool append_platform_extension) {
  try {
    auto module_factory = impl_->module_lib_holder_->AppendModuleLib(module_lib_path, append_platform_extension);
    if (!module_factory) {
      return false;
    }

    return RegisterModuleFactory(std::move(module_factory));
  } catch (std::runtime_error ex) {
    LOG_CRITICAL("Core::RegisterModuleFactory error: {}", ex.what());
    throw;
  }
  return false;
}

WorldId Core::CreateWorld() {
  auto it = impl_->worlds_.insert({impl_->new_world_id_, flecs::world()});
  ++impl_->new_world_id_;

  auto& world = it.first->second;
  world.component<CoreComponent>();
  CoreComponent core_component {.core = *this};
  world.set(core_component);
  for (auto& module_factory_ptr : impl_->module_factories_) {
    module_factory_ptr->RegisterModules(world);
  }

  world.add<RegisterComponentsEvent>();
  world.add<InitPhasesEvent>();
  world.add<InitSystemsEvent>();
  world.add<InitWorldDataEvent>();

  return it.first->first;
}

void Core::Update(float delta_time) {
  for (auto& [_, world] : impl_->worlds_) {
    world.progress(delta_time);
  }
}

void Core::Shutdown() {
  impl_->pending_shutdown_ = true;
}

bool Core::IsPendingShutDown() const {
  return impl_->pending_shutdown_;
}

int Core::Run() {
  if (impl_->config_.NeedShowHelp()) {
    std::cout << impl_->config_ << "\n";

    return 0;
  }

  if (impl_->config_.GetFPS() <= std::numeric_limits<double>::epsilon()) {
    return 1;
  }

  const auto update_time = 1. / impl_->config_.GetFPS();
  std::chrono::duration<double> sleep_time(update_time), frame_delta(update_time);
  auto accumulated_frame_time = std::chrono::duration<double>::zero();
  auto sleep_duration = sleep_time;
  auto prev = std::chrono::high_resolution_clock::now();
  while (!impl_->worlds_.empty() && !IsPendingShutDown()) {
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
