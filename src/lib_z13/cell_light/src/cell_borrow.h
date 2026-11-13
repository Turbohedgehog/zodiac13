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

#include <span>
#include <vector>

#include <Eigen/Dense>

#include <primitives/cell_box.h>

#include <cell_light/light_layout.h>

#include "light_nodes.h"

namespace z13::station::light {

// The texels of `cells`, which lie inside the layout. An opaque cell borrows the free cell
// nearest to it within reach, a lit node's before any unlit one's (`light` per node).
std::vector<CellTexel> CellTexelsOf(const LightNodes& nodes, std::span<const Eigen::Vector3f> light,
                                    const z13::building::primitives::CellBox& cells);

}  // namespace z13::station::light
