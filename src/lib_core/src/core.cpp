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
#include <lib_core/tick_pacer.h>
#include <lib_core/module_factory_base.h>
#include <lib_core/lifecycle.h>
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

Core::Core(int argc, char *argv[], const ConfigureOptions& configure_options)
  : module_lib_holder_(std::make_unique<ModuleLibHolder>()) {
  // : module_lib_holder_(std::make_shared<ModuleLibHolder>()) {
  if (configure_options) {
    if (const auto configured = configure_options(config_); !configured) {
      config_error_ = configured.error();
      return;
    }
  }
  if (const auto result = config_.ParseCommandLineArguments(argc, argv); !result) {
    config_error_ = result.error();
  }
}

Core::~Core() = default;

const Config& Core::GetConfig() const {
  return config_;
}

Config& Core::GetConfig() {
  return config_;
}

std::optional<std::string> Core::GetConfigError() const {
  return config_error_;
}

bool Core::RegisterModuleFactory(ModuleFactoryPtr module_factory) {
  return !!module_factories_.emplace_back(module_factory);
}

std::expected<void, std::string> Core::RegisterModuleFactory(
    const std::filesystem::path& module_lib_path, bool append_platform_extension) {
  return module_lib_holder_->AppendModuleLib(module_lib_path, append_platform_extension)
      .transform([this](ModuleFactoryPtr module_factory) { RegisterModuleFactory(std::move(module_factory)); });
}

std::expected<WorldRef, std::string> Core::CreateWorld() {
  auto it = worlds_.insert({new_world_id_, flecs::world()});
  ++new_world_id_;

  auto& world = it.first->second;
  z13::flecs_tools::RegisterStateMeta(world);
  world.set(ActiveCoreSettings(config_.GetCoreSettings()));
  world.component<CoreComponent>();
  CoreComponent core_component {.core = *this};
  world.set(core_component);
  // See ModuleFactoryBase::SyncFlecsOsApi: this world's flecs::world() constructor
  // (above) already initialized this process's os_api; hand that same value to
  // each module before it makes its own first flecs call.
  const ecs_os_api_t os_api = ecs_os_get_api();
  InitLifecycle(world);
  for (auto& module_factory_ptr : module_factories_) {
    module_factory_ptr->SyncFlecsOsApi(os_api);
    module_factory_ptr->RegisterModules(world);
  }

  if (auto created = RunLifecycle(world).and_then([&] { return flecs_tools::ValidateStateComponents(world); });
      !created) {
    worlds_.erase(it.first);
    return std::unexpected(std::move(created.error()));
  }

  return WorldRef(world);
}

void Core::Update(float delta_time, flecs_tools::FrameKind kind) {
  for (auto& [_, world] : worlds_) {
    z13::flecs_tools::TickWorld(world, delta_time, kind);
  }
}

void Core::Shutdown(int exit_code) {
  pending_shutdown_ = true;
  exit_code_ = exit_code;
}

bool Core::IsPendingShutDown() const {
  return pending_shutdown_;
}

int Core::ExitCode() const {
  return exit_code_;
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

  // A joined client's fps and backlog come from the server and change mid-run.
  struct PaceSettings {
    double fps {};
    uint64_t max_backlog {};
    bool operator==(const PaceSettings&) const = default;
  };
  const auto current_pace = [this] {
    return PaceSettings {.fps = config_.GetFPS(), .max_backlog = config_.GetCoreSettings().max_tick_backlog};
  };
  const auto make_pacer = [](const PaceSettings& pace) {
    return TickPacer(
        std::chrono::duration_cast<TickPacer::Clock::duration>(std::chrono::duration<double>(1. / pace.fps)),
        TickPacer::Clock::now(), pace.max_backlog);
  };

  PaceSettings pace = current_pace();
  TickPacer pacer = make_pacer(pace);
  while (!worlds_.empty() && !IsPendingShutDown()) {
    if (interrupt_signal_) {
      log_info("Core::Run: received signal {}, shutting down", static_cast<int>(interrupt_signal_));
      interrupt_signal_ = 0;
      Shutdown();
      continue;
    }

    if (const PaceSettings latest = current_pace(); latest != pace) {
      pace = latest;
      pacer = make_pacer(pace);
    }

    if (!pacer.TakeTick(TickPacer::Clock::now())) {
      std::this_thread::sleep_until(pacer.NextTickTime());
      continue;
    }

    const bool behind = TickPacer::Clock::now() >= pacer.NextTickTime();
    Update(1. / pace.fps, behind ? flecs_tools::FrameKind::kCatchUp : flecs_tools::FrameKind::kLive);
  }

  return exit_code_;
}

}  // namespace z13
