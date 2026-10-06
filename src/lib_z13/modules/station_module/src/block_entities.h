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

#include <string>

#include <flecs.h>

#include <z13/components/station.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_settings/building_tuning.h>

namespace z13::station {

// A state entity for `block`; a primitive with the Spawn flag also makes it a SpawnPoint,
// facing the primitive's +X.
flecs::entity CreateBlock(
    flecs::world world, const std::string& name, const Block& block, const z13::building::primitives::Palette& palette,
    const BuildingTuning& tuning);

// The cells above a spawn marker kept free so players appear in the open: its footprint,
// up past the head of a player at the spawn height.
z13::building::primitives::CellBox SpawnClearance(const Block& marker, const BuildingTuning& tuning);

}  // namespace z13::station
