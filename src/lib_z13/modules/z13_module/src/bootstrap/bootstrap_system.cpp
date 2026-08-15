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

module;

#include <flecs.h>

module z13.module.bootstrap;

import z13.core;
import z13.components;

namespace z13::bootstrap {

namespace {

struct BootstrapComponent {};
struct BootstrapCompleteComponent {};

void RegisterComponents(flecs::world& world) {
  world.entity().add<BootstrapComponent>();
  world.component<BootstrapCompleteComponent>().add(flecs::Singleton);
}

void InitBootstrap(flecs::entity e, const BootstrapComponent&) {
  // todo: завязать все загрузки и инициализации систем на последовательности, указанной здесь.
  // Последовательность будет расширена.
  e.add<LoadConfigEvent>();
  e.add<CreatePlayerEvent>();

  e.world().add<BootstrapCompleteComponent>();
}

}  // namespace

void BootstrapSystem::Register(flecs::world& world) {
  world.system<BootstrapComponent>("InitBootstrap")
    .kind<z13::gameplay::PreUpdatePhase>()
    .without<BootstrapCompleteComponent>()
    .each(InitBootstrap);
}

}  // namespace z13::bootstrap
