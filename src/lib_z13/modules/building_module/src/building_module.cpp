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

#include "building_module.h"

#include <flecs.h>

#include "block_building_system.h"
#include "brush_system.h"
#include "building_input_system.h"
#include "building_system.h"
#include "construction_site_system.h"
#include "palette_system.h"

namespace z13::building {

BuildingModule::BuildingModule(flecs::world& world) {
  BuildingSystem::Register(world);
  BuildingInputSystem::Register(world);
  PaletteSystem::Register(world);
  ConstructionSiteSystem::Register(world);
  // Before BlockBuildingSystem: same phase, so the brush is picked before it builds.
  BrushSystem::Register(world);
  BlockBuildingSystem::Register(world);
}

}  // namespace z13::building
