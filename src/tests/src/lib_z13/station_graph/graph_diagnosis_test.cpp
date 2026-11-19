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

#include <station_graph/graph_diagnosis.h>
#include <z13_tests/shipped_station.h>

namespace z13::editor::graph {
namespace {

constexpr int kRoomCells = 12;

Node RoomAt(std::string id, int x, int y) {
  return {.id = std::move(id), .box = {.origin = {x, y, 0}, .size = {kRoomCells, kRoomCells}}};
}

PropertySchema Schema() {
  const auto schema = z13::testing::ShippedPropertySchema();
  EXPECT_TRUE(schema) << schema.error();
  return schema.value_or(PropertySchema {});
}

TEST(GraphDiagnosisTest, TheShippedPrototypeGraphHasNoProblems) {
  const auto graph = z13::testing::ShippedGraph(z13::testing::kPrototypeGraph);
  ASSERT_TRUE(graph) << graph.error();

  EXPECT_TRUE(Diagnose(*graph, Schema()).empty());
}

TEST(GraphDiagnosisTest, RoomsMeetingByOneWallAreFine) {
  const Graph graph {.nodes = {RoomAt("a", 0, 0), RoomAt("b", kRoomCells - 1, 0)},
                     .edges = {{.id = "a-b", .from = "a", .to = "b"}}};

  EXPECT_TRUE(Diagnose(graph, Schema()).empty());
}

TEST(GraphDiagnosisTest, RoomsOverlappingByMoreThanAWallAreReported) {
  const Graph graph {.nodes = {RoomAt("a", 0, 0), RoomAt("b", kRoomCells - 2, 0)}};

  const std::vector<std::string> problems = Diagnose(graph, Schema());

  ASSERT_EQ(problems.size(), 1U);
  EXPECT_NE(problems.front().find("overlap"), std::string::npos);
}

TEST(GraphDiagnosisTest, AnEdgeBetweenRoomsThatShareNoWallIsReported) {
  const Graph graph {.nodes = {RoomAt("a", 0, 0), RoomAt("far", kRoomCells * 3, 0)},
                     .edges = {{.id = "a-far", .from = "a", .to = "far"}}};

  const std::vector<std::string> problems = Diagnose(graph, Schema());

  ASSERT_EQ(problems.size(), 1U);
  EXPECT_NE(problems.front().find("a-far"), std::string::npos);
}

TEST(GraphDiagnosisTest, RoomsOnDifferentLevelsDoNotCollide) {
  Node upper = RoomAt("upper", 0, 0);
  upper.box.origin.z() = kRoomCells;
  const Graph graph {.nodes = {RoomAt("a", 0, 0), upper}};

  EXPECT_TRUE(Diagnose(graph, Schema()).empty());
}

}  // namespace
}  // namespace z13::editor::graph
