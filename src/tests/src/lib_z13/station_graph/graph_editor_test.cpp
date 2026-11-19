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

#include <string>

#include <station_graph/graph_editor.h>
#include <z13_tests/shipped_station.h>

namespace z13::editor::graph {
namespace {

constexpr int kRoomCells = 12;
constexpr int kBeside = kRoomCells - 1;  // the next room's box overlaps this one's by the shared wall
constexpr int kHeight = 20;

GraphEditor ShippedEditor() {
  auto schema = z13::testing::ShippedPropertySchema();
  EXPECT_TRUE(schema) << schema.error();
  return GraphEditor({}, schema.value_or(PropertySchema {}));
}

Node RoomAt(std::string id, int x) {
  return {.id = std::move(id), .box = {.origin = {x, 0, 0}, .size = {kRoomCells, kRoomCells}}};
}

GraphEditor TwoJoinedRooms() {
  GraphEditor editor = ShippedEditor();
  EXPECT_TRUE(editor.AddRoom(RoomAt("a", 0)));
  EXPECT_TRUE(editor.AddRoom(RoomAt("b", kBeside)));
  EXPECT_TRUE(editor.AddEdge({.id = "a-b", .from = "a", .to = "b"}));
  return editor;
}

TEST(GraphEditorTest, ARoomTakesItsNameFromItsId) {
  GraphEditor editor = ShippedEditor();

  ASSERT_TRUE(editor.AddRoom(RoomAt("a", 0)));

  EXPECT_EQ(editor.graph().nodes.front().name, "a");
}

TEST(GraphEditorTest, RefusesWhatLeavesTheGraphOutOfShapeAndChangesNothing) {
  GraphEditor editor = TwoJoinedRooms();
  const Graph before = editor.graph();

  EXPECT_FALSE(editor.AddRoom(RoomAt("a", kRoomCells * 3)));
  EXPECT_FALSE(editor.AddRoom(RoomAt("", kRoomCells * 3)));
  EXPECT_FALSE(editor.AddRoom({.id = "tiny", .box = {.size = {1, 1}}}));
  EXPECT_FALSE(editor.AddRoom({.id = "odd", .box = {.size = {kRoomCells, kRoomCells}}, .properties = {{"colour", true}}}));
  EXPECT_FALSE(editor.AddEdge({.id = "a-b", .from = "a", .to = "b"}));
  EXPECT_FALSE(editor.AddEdge({.id = "loop", .from = "a", .to = "a"}));
  EXPECT_FALSE(editor.AddEdge({.id = "lost", .from = "a", .to = "nowhere"}));
  EXPECT_FALSE(editor.SetNodeProperty("a", "height", 1));
  EXPECT_FALSE(editor.SetNodeProperty("nowhere", "height", kHeight));
  EXPECT_FALSE(editor.RemoveEdge("nowhere"));

  EXPECT_EQ(editor.graph(), before);
}

TEST(GraphEditorTest, RemovingARoomRemovesItsEdges) {
  GraphEditor editor = TwoJoinedRooms();

  ASSERT_TRUE(editor.RemoveNode("a"));

  EXPECT_EQ(editor.graph().nodes.size(), 1U);
  EXPECT_TRUE(editor.graph().edges.empty());
}

TEST(GraphEditorTest, SetsPropertiesAndTheBoxOfARoom) {
  GraphEditor editor = TwoJoinedRooms();

  ASSERT_TRUE(editor.SetNodeProperty("a", "height", kHeight));
  ASSERT_TRUE(editor.SetEdgeProperty("a-b", "kind", std::string("Passage")));
  ASSERT_TRUE(editor.SetRoomBox("b", {.origin = {kBeside, 0, 0}, .size = {kRoomCells * 2, kRoomCells}}));

  EXPECT_EQ(std::get<int>(editor.graph().nodes[0].properties.at("height")), kHeight);
  EXPECT_EQ(std::get<std::string>(editor.graph().edges[0].properties.at("kind")), "Passage");
  EXPECT_EQ(editor.graph().nodes[1].box.size.x(), kRoomCells * 2);
}

TEST(GraphEditorTest, UndoAndRedoWalkTheEditsBackAndForward) {
  GraphEditor editor = ShippedEditor();
  ASSERT_TRUE(editor.AddRoom(RoomAt("a", 0)));
  ASSERT_TRUE(editor.SetNodeProperty("a", "height", kHeight));
  const Graph edited = editor.graph();

  ASSERT_TRUE(editor.Undo());
  EXPECT_TRUE(editor.graph().nodes.front().properties.empty());
  ASSERT_TRUE(editor.Undo());
  EXPECT_TRUE(editor.graph().nodes.empty());
  EXPECT_FALSE(editor.CanUndo());
  EXPECT_FALSE(editor.Undo());

  ASSERT_TRUE(editor.Redo());
  ASSERT_TRUE(editor.Redo());
  EXPECT_EQ(editor.graph(), edited);
  EXPECT_FALSE(editor.CanRedo());
}

TEST(GraphEditorTest, ANewEditEndsTheRedoList) {
  GraphEditor editor = ShippedEditor();
  ASSERT_TRUE(editor.AddRoom(RoomAt("a", 0)));
  ASSERT_TRUE(editor.Undo());
  ASSERT_TRUE(editor.CanRedo());

  ASSERT_TRUE(editor.AddRoom(RoomAt("b", 0)));

  EXPECT_FALSE(editor.CanRedo());
}

TEST(GraphEditorTest, TheHistoryKeepsTheLastEditsOnly) {
  GraphEditor editor = ShippedEditor();
  ASSERT_TRUE(editor.AddRoom(RoomAt("a", 0)));
  for (std::size_t i = 0; i < kMaxHistory + 1; ++i) {
    ASSERT_TRUE(editor.SetNodeProperty("a", "lamp", i % 2 == 0));
  }

  std::size_t undone = 0;
  while (editor.Undo()) {
    ++undone;
  }

  EXPECT_EQ(undone, kMaxHistory);
}

TEST(GraphEditorTest, ReplacingTheGraphStartsTheHistoryAnew) {
  GraphEditor editor = TwoJoinedRooms();

  editor.Replace({});

  EXPECT_TRUE(editor.graph().nodes.empty());
  EXPECT_FALSE(editor.CanUndo());
  EXPECT_FALSE(editor.CanRedo());
}

}  // namespace
}  // namespace z13::editor::graph
