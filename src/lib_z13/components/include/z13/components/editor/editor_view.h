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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace z13::editor {

// The tag of the editor's commands: the window sends them, the plugin answers them.
inline constexpr std::string_view kEditorTagName = "editor";

enum class EditorPropertyKind : std::uint8_t { kInt, kBool, kEnum };

// One property of a room or an edge, as the schema describes it with the value now (a value as a command takes it).
struct EditorPropertyView {
  std::string name;
  std::string title;
  std::string unit;
  EditorPropertyKind kind {};
  int min {};
  int max {};
  std::vector<std::string> values;
  std::string value;
};

struct EditorItemView {
  std::string id;
  std::string label;
  std::string detail;
  std::vector<EditorPropertyView> properties;
};

// What the editor's window shows of the graph being edited, filled by the plugin while `open`. Client only, never state.
struct EditorView {
  using Singleton = void;

  bool open {};
  bool loaded {};
  std::vector<EditorItemView> rooms;
  std::vector<EditorItemView> edges;
  std::vector<std::string> problems;
};

}  // namespace z13::editor
