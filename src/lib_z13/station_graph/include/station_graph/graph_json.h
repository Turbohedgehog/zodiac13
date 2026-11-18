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

#include <expected>
#include <string>
#include <string_view>

#include "station_graph/graph.h"

namespace z13::editor::graph {

// The graph file (assets/station/graph/<name>.json); a file of another version or with a field out of shape is an error.
std::expected<Graph, std::string> ParseGraph(std::string_view json);
std::string WriteGraph(const Graph& graph);

}  // namespace z13::editor::graph
