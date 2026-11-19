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
#include <map>
#include <string>
#include <variant>
#include <vector>

#include <flecs.h>

#include <z13/components/editor/editor_view.h>

namespace z13::raylib::gui {

// A command the window asks of the editor; run by a system outside the drawing one, which may not write entities.
using EditorArgument = std::variant<std::string, std::int64_t>;

struct EditorRequest {
  std::string command;
  std::map<std::string, EditorArgument> arguments;
};

// Asks the editor's command, logging a refusal like the console does.
void RunEditorRequest(flecs::world world, const EditorRequest& request);

// The station editor's window (docs/station-editor-plan.md): the graph's rooms and edges, and the properties of the
// one picked, built from the schema. It only shows `EditorView` and asks for commands. Singleton; never state.
class EditorWindow {
 public:
  using Singleton = void;

  void Draw(const z13::editor::EditorView& view);

  std::vector<EditorRequest> TakeRequests();

 private:
  void DrawList(const z13::editor::EditorView& view);
  void DrawProperties(const z13::editor::EditorView& view);
  void DrawProperty(const z13::editor::EditorItemView& item, bool edge, const z13::editor::EditorPropertyView& property);
  void DrawBox(const z13::editor::EditorItemView& item);
  void Ask(std::string command, std::map<std::string, EditorArgument> arguments = {});

  std::vector<EditorRequest> requests_;
};

}  // namespace z13::raylib::gui
