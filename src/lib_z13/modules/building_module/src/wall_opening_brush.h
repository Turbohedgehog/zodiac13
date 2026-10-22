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

#pragma once

#include <optional>
#include <vector>

#include <flecs.h>

#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/wall.h>
#include <z13_settings/building_tuning.h>

#include "brush_aim.h"

namespace z13::building {

// The opening the Opening mode would cut where the aim meets a wall, and its outline on both
// faces.
struct AimedOpening {
  z13::station::WallOpening opening;
  std::vector<z13::station::BrushLine> lines;
};

// A door to the floor or a window at the tuned sill, as wide as the tool says and centred on the
// aim in WallSteps, kept off the ends of the wall and cut down to leave wall above it. Nothing
// unless the aim is on a wall that takes openings.
std::optional<AimedOpening> AimOpening(
    flecs::world world, const Aim& aim, const BuildTool& tool, const BuildingTuning& tuning);

// The opening the aim looks through or at, if any.
std::optional<flecs::entity> OpeningUnder(flecs::world world, const Aim& aim);

}  // namespace z13::building
