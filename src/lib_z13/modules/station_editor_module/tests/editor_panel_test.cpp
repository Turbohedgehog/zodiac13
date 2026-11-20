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

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <z13/components/editor/editor_view.h>
#include <z13/components/gui/gui_panels.h>
#include <z13_tests/shipped_station.h>

#include "editor_test_support.h"

namespace z13::station_editor {
namespace {

using z13::gui::PanelWidget;
using z13::gui::PanelWidgetKind;
using z13::testing::Z13TestWorld;

constexpr std::string_view kPanelId = "station_editor";

rfl::Generic Str(std::string value) {
  return rfl::Generic(std::move(value));
}

const z13::gui::Panel& EditorPanel(Z13TestWorld& test_world) {
  const auto& panels = test_world.World().get<z13::gui::GuiPanels>().panels;
  return *std::ranges::find(panels, kPanelId, &z13::gui::Panel::id);
}

const PanelWidget* Widget(const z13::gui::Panel& panel, std::string_view id) {
  const auto found = std::ranges::find(panel.widgets, id, &PanelWidget::id);
  return found != panel.widgets.end() ? &*found : nullptr;
}

class EditorPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_world_.Tick();
    ASSERT_TRUE(RunCommand(test_world_, "editor_load", {{"name", Str(std::string(z13::testing::kPrototypeGraph))}}));
    ASSERT_TRUE(RunCommand(test_world_, "editor_window"));
    test_world_.Tick();
  }

  Z13TestWorld test_world_;
};

TEST_F(EditorPanelTest, TheOpenWindowIsAPanelWithARowForEachRoomAndEdge) {
  const z13::gui::Panel& panel = EditorPanel(test_world_);

  EXPECT_TRUE(panel.open);
  EXPECT_EQ(panel.on_close.command, "editor_window");
  for (const std::string_view id : {"node_hall", "node_corridor", "node_lab", "edge_hall-corridor", "edge_corridor-lab"}) {
    const PanelWidget* row = Widget(panel, id);
    ASSERT_NE(row, nullptr) << id;
    EXPECT_EQ(row->kind, PanelWidgetKind::kChoice);
    EXPECT_EQ(row->action.command, "editor_select");
    EXPECT_EQ(row->action.tag, z13::editor::kEditorTagName);
  }
  EXPECT_NE(Widget(panel, "editor_undo"), nullptr);
  EXPECT_NE(Widget(panel, "editor_pick"), nullptr);
}

TEST_F(EditorPanelTest, ThePickedRoomLightsItsRowAndShowsItsBoxAndPropertiesByTheirKind) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_select", {{"target", Str("node")}, {"id", Str("hall")}}));
  test_world_.Tick();
  const z13::gui::Panel& panel = EditorPanel(test_world_);

  EXPECT_TRUE(Widget(panel, "node_hall")->chosen);
  EXPECT_FALSE(Widget(panel, "node_lab")->chosen);
  const PanelWidget* width = Widget(panel, "hall_box_w");
  ASSERT_NE(width, nullptr);
  EXPECT_EQ(width->kind, PanelWidgetKind::kNumber);
  EXPECT_EQ(width->action.command, "editor_set_room");
  EXPECT_EQ(width->action.value_argument, "w");
  EXPECT_EQ(std::get<std::int64_t>(width->action.arguments.at("w")), width->value);
  const PanelWidget* height = Widget(panel, "node_hall_height");
  ASSERT_NE(height, nullptr);
  EXPECT_EQ(height->kind, PanelWidgetKind::kNumber);
  EXPECT_LT(height->min, height->max);
  EXPECT_EQ(height->action.command, "editor_set_property");
  EXPECT_TRUE(height->action.value_as_text);
  const PanelWidget* spawn = Widget(panel, "node_hall_spawn");
  ASSERT_NE(spawn, nullptr);
  EXPECT_EQ(spawn->kind, PanelWidgetKind::kToggle);
}

TEST_F(EditorPanelTest, ThePickedEdgeShowsItsKindAsAPicker) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_select", {{"target", Str("edge")}, {"id", Str("hall-corridor")}}));
  test_world_.Tick();

  const PanelWidget* kind = Widget(EditorPanel(test_world_), "edge_hall-corridor_kind");

  ASSERT_NE(kind, nullptr);
  EXPECT_EQ(kind->kind, PanelWidgetKind::kPicker);
  EXPECT_EQ(kind->current, "Door");
  EXPECT_FALSE(kind->options.empty());
}

TEST_F(EditorPanelTest, ClosingTheWindowClosesThePanel) {
  ASSERT_TRUE(RunCommand(test_world_, "editor_window"));
  test_world_.Tick();

  EXPECT_FALSE(EditorPanel(test_world_).open);
}

}  // namespace
}  // namespace z13::station_editor
