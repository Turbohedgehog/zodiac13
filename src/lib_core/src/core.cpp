/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lib_core/core.h>

#include <csignal>
#include <vector>
#include <iostream>
#include <chrono>
#include <limits>
#include <thread>
#include <typeinfo>

#include <lib_core/components.h>
#include <lib_core/rollback.h>
#include <lib_core/module_factory_base.h>
#include <lib_core/log.h>
#include <lib_core/world_state.h>

#include "module_lib_holder.h"

namespace z13 {

namespace {

void HandleInterruptSignal(int signal_number) {
  Core::RequestInterrupt(signal_number);
}

}  // namespace

volatile std::sig_atomic_t Core::interrupt_signal_ = 0;

Core::Core(int argc, char *argv[])
  : module_lib_holder_(std::make_unique<ModuleLibHolder>()) {
  // : module_lib_holder_(std::make_shared<ModuleLibHolder>()) {
  if (const auto result = config_.ParseCommandLineArguments(argc, argv); !result) {
    config_error_ = result.error();
  }
}

Core::~Core() = default;

const Config& Core::GetConfig() const {
  return config_;
}

std::optional<std::string> Core::GetConfigError() const {
  return config_error_;
}

bool Core::RegisterModuleFactory(ModuleFactoryPtr module_factory) {
  return !!module_factories_.emplace_back(module_factory);
  // return true;
  // return module_factories_.insert({module_factory->GetName(), module_factory}).second;
}

bool Core::RegisterModuleFactory(const std::filesystem::path& module_lib_path, bool append_platform_extension) {
  try {
    auto module_factory = module_lib_holder_->AppendModuleLib(module_lib_path, append_platform_extension);
    if (!module_factory) {
      return false;
    }

    return RegisterModuleFactory(module_factory);
  } catch (std::runtime_error ex) {
    log_critical("Core::RegisterModuleFactory error: {}", ex.what());
    throw;
  }
  return false;
}

WorldRef Core::CreateWorld() {
  auto it = worlds_.insert({new_world_id_, flecs::world()});
  ++new_world_id_;

  auto& world = it.first->second;
  z13::flecs_tools::RegisterStateMeta(world);
  world.component<CoreComponent>();
  CoreComponent core_component {.core = *this};
  world.set(core_component);
  // See ModuleFactoryBase::SyncFlecsOsApi: this world's flecs::world() constructor
  // (above) already initialized this process's os_api; hand that same value to
  // each module before it makes its own first flecs call.
  const ecs_os_api_t os_api = ecs_os_get_api();
  for (auto& module_factory_ptr : module_factories_) {
    module_factory_ptr->SyncFlecsOsApi(os_api);
    module_factory_ptr->RegisterModules(world);
  }

  world.add<RegisterComponentsEvent>();
  world.add<InitPhasesEvent>();
  world.add<InitSystemsEvent>();
  world.add<InitWorldDataEvent>();

  return WorldRef(world);
}

void Core::Update(float delta_time) {
  for (auto& [_, world] : worlds_) {
    z13::flecs_tools::TickWorld(world, delta_time);
  }
}

void Core::Shutdown() {
  pending_shutdown_ = true;
}

bool Core::IsPendingShutDown() const {
  return pending_shutdown_;
}

void Core::RequestInterrupt(int signal_number) {
  interrupt_signal_ = signal_number;
}

int Core::Run() {
  if (config_.NeedShowHelp()) {
    std::cout << config_ << "\n";

    return 0;
  }

  if (const auto error = GetConfigError()) {
    log_error("{}", *error);
    return 1;
  }

  if (config_.GetFPS() <= std::numeric_limits<double>::epsilon()) {
    return 1;
  }

  interrupt_signal_ = 0;
  std::signal(SIGINT, HandleInterruptSignal);
  std::signal(SIGTERM, HandleInterruptSignal);

  const auto update_time = 1. / config_.GetFPS();
  std::chrono::duration<double> sleep_time(update_time), frame_delta(update_time);
  auto accumulated_frame_time = std::chrono::duration<double>::zero();
  auto sleep_duration = sleep_time;
  auto prev = std::chrono::high_resolution_clock::now();
  while (!worlds_.empty() && !IsPendingShutDown()) {
    if (interrupt_signal_) {
      log_info("Core::Run: received signal {}, shutting down", static_cast<int>(interrupt_signal_));
      interrupt_signal_ = 0;
      Shutdown();
      continue;
    }

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
