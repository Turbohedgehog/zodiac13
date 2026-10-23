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

#include <primitives/station_viewpoints.h>

#include <primitives/station_assets.h>

namespace z13::building::primitives {

std::vector<StationViewpoint> StationViewpoints(std::string_view scene) {
  if (scene == kOctantScene) {
    // Where frames took 52 ms (19 fps) on a PC in Release, 2026-10-22: a hall room culling can't cut.
    return {{.label = "the atrium", .position_m = {193.32f, 80.17f}}};
  }
  return {};
}

}  // namespace z13::building::primitives
