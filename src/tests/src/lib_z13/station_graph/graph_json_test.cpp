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

#include <station_graph/graph_json.h>
#include <z13_tests/shipped_station.h>

namespace z13::editor::graph {
namespace {

constexpr std::string_view kHall = "hall";
constexpr std::string_view kOtherVersion = R"({"version": 99, "nodes": [], "edges": []})";

TEST(GraphJsonTest, TheShippedPrototypeGraphReadsAndWritesBackTheSame) {
  const auto graph = z13::testing::ShippedGraph(z13::testing::kPrototypeGraph);
  ASSERT_TRUE(graph) << graph.error();
  ASSERT_FALSE(graph->nodes.empty());
  ASSERT_FALSE(graph->edges.empty());

  const auto again = ParseGraph(WriteGraph(*graph));

  ASSERT_TRUE(again) << again.error();
  EXPECT_EQ(*again, *graph);
}

TEST(GraphJsonTest, ReadsTheBoxAndThePropertiesOfARoom) {
  const auto graph = z13::testing::ShippedGraph(z13::testing::kPrototypeGraph);
  ASSERT_TRUE(graph) << graph.error();
  const auto hall = FindNode(*graph, kHall);
  ASSERT_TRUE(hall);

  const Node& node = graph->nodes[*hall];

  EXPECT_EQ(node.box.origin, Eigen::Vector3i::Zero());
  EXPECT_GT(node.box.size.x(), 0);
  EXPECT_TRUE(std::get<bool>(node.properties.at("spawn")));
}

TEST(GraphJsonTest, AFileOfAnotherVersionOrOutOfShapeIsRefused) {
  EXPECT_FALSE(ParseGraph(kOtherVersion));
  EXPECT_FALSE(ParseGraph("not json"));
  EXPECT_FALSE(ParseGraph(R"({"version": 1, "nodes": [{"id": "a"}], "edges": []})"));
}

}  // namespace
}  // namespace z13::editor::graph
