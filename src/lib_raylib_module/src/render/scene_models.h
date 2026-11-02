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

#include <flecs.h>

#include <lib_core/utils/asset_source.h>
#include <z13_settings/settings.h>

#include "render_components.h"

namespace z13::raylib {

// The lighting shader and the models drawn with it: the avatar, the ordinary scene's spaceship, a station's props.
Lighting LoadLighting(const AssetSource& assets);

AvatarModel LoadAvatar(const Lighting& lighting, const z13::RenderTuning& tuning);

// The ship belongs to the ordinary scene alone, so a station game never parses its model.
void ReconcileSpaceship(flecs::world world, const Lighting& lighting, const AssetSource& assets,
                        const z13::RenderTuning& tuning);

// Only a host knows its scene: a joining client's station comes without its props.
void ReconcileStationProps(
    flecs::world world, const Lighting& lighting, const AssetSource& assets, const z13::RenderTuning& tuning);

}  // namespace z13::raylib
