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

#include <station_graph/graph_editor.h>

namespace z13::station_editor {

// The graph being edited and its history. Empty until `editor_load`; its source is a file, so it is not state.
struct EditorGraph {
  using Singleton = void;

  std::optional<z13::editor::graph::GraphEditor> editor;
};

}  // namespace z13::station_editor
