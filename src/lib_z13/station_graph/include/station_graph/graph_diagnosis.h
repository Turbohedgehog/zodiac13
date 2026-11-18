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
#include <vector>

#include "station_graph/graph.h"

namespace z13::editor::graph {

// What keeps the graph from being built, one line each: rooms overlapping by more than a shared wall, and edges between
// rooms that share no wall. The pipeline finds the rest (an opening too wide for its wall, a wall too low).
std::vector<std::string> Diagnose(const Graph& graph);

}  // namespace z13::editor::graph
