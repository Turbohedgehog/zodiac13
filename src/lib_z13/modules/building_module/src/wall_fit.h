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

#include <lib_core/utils/status.h>
#include <z13/components/wall.h>
#include <primitives/wall_panel.h>

#include "build_validation.h"

namespace z13::building {

// Whether `wall` can be laid: no joint breaks, and its panel and the neighbours it re-cuts
// fit, not counting the panels they replace.
Status CheckWallFits(flecs::world world, const z13::building::primitives::WallGeometry& wall, const BuildView& view);

// Whether `opening` can be cut in its segment beside those there: the wall's pieces come out
// and fit, not counting the panels they replace.
Status CheckOpeningFits(flecs::world world, const z13::station::WallOpening& opening, const BuildView& view);

}  // namespace z13::building
