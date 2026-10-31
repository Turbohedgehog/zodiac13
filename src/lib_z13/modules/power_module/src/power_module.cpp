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


#include "power_module.h"

#include <cstdint>
#include <string>
#include <string_view>

#include <flecs.h>

#include <lib_core/world/lifecycle.h>
#include <power/port_names.h>
#include <z13/components/utilities.h>

#include "power_overlay_system.h"
#include "power_system.h"

namespace z13::power_module {

namespace {

// Grows with any change that makes the balance differ.
constexpr uint32_t kModuleVersion = 1;

}  // namespace

PowerModule::PowerModule(flecs::world& world) {
  PowerSystem::Register(world);
  PowerOverlaySystem::Register(world);
  // In the phases stage, once building_module has made the list (see UtilityModules).
  z13::OnInitPhases(world, [](flecs::world& w) {
    z13::utility::DeclareUtilityModule(w, {.name = std::string(kModuleName), .version = kModuleVersion, .palette_fragment = true});
  });
}

}  // namespace z13::power_module
