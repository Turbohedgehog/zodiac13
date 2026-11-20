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

#include "station_graph/graph.h"
#include "station_graph/property_schema.h"

namespace z13::editor::graph {

// The heights, in cells and ascending, at which the view can be cut to show one deck from above: just under the
// ceiling slab of the tallest room of each level.
std::vector<float> DeckCuts(const Graph& graph, const PropertySchema& schema);

// The cut `step` decks from `current` (negative: down, stopping at the lowest); past the highest deck the cut is off.
// A `current` that is no longer one of `cuts` counts as the next one above it; none is the position above the highest.
std::optional<float> SteppedCut(const std::vector<float>& cuts, std::optional<float> current, int step);

}  // namespace z13::editor::graph
