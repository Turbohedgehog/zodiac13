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

#include <vector>

#include <primitives/placement.h>
#include <sectors/sector.h>
#include <z13/components/station/block.h>
#include <z13/components/station/grid_pose.h>

namespace z13::net {

// The sectors a block touches in the world: its cells moved by the pose of its grid.
std::vector<sectors::SectorKey> BlockSectors(
    const z13::station::Block& block, const z13::station::GridPoses& poses, int sector_cells);

}  // namespace z13::net
