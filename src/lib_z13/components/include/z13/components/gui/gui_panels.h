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

namespace z13::gui {

using PanelArgument = std::variant<std::string, std::int64_t>;

// A command the panel asks when its widget is used. `value_argument` names the argument that takes the widget's new
// value (a number, a flag as "true"/"false", or the picked text), as text when `value_as_text`.
struct PanelAction {
  std::string command;
  // The tag of the channel that asks it (the commands' `Tags`).
  std::string tag;
  std::map<std::string, PanelArgument> arguments;
  std::string value_argument;
  bool value_as_text {};
};

enum class PanelWidgetKind : std::uint8_t { kHeading, kText, kProblem, kButton, kChoice, kNumber, kToggle, kPicker };

struct PanelWidget {
  PanelWidgetKind kind {};
  // Unique in its panel.
  std::string id;
  // The text of a heading, text or problem; the caption of the rest.
  std::string label;
  // Next to the widget before it, not below.
  bool same_line {};
  // A choice (a list row) is lit while chosen; a toggle is on.
  bool chosen {};
  int value {};
  int min {};
  int max {};
  std::vector<std::string> options;
  // What a picker shows now.
  std::string current;
  PanelAction action;
};

// A window the modules describe and the renderer draws, knowing nothing of what it is for: the panel's owner rebuilds
// it while `open` and answers `on_close` and its widgets' actions.
struct Panel {
  std::string id;
  std::string title;
  bool open {};
  std::vector<PanelWidget> widgets;
  PanelAction on_close;
};

// Every panel, by id. Client only, never state.
struct GuiPanels {
  using Singleton = void;
  std::vector<Panel> panels;
};

}  // namespace z13::gui
