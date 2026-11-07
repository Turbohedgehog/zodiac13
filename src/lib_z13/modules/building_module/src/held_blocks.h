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

#include <z13/components/station/block.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/held_sectors.h>

namespace z13::building {

// Whether a world that holds only some sectors (a client under interest) holds this block: it touches one of them,
// in the world by the pose of its grid. Every block of a world holding all.
bool TouchesHeldSectors(const z13::station::Block& block, const z13::station::HeldSectors& held,
                        const z13::station::GridPoses& poses);

}  // namespace z13::building
