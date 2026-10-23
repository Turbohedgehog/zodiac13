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
#include <numeric>
#include <random>
#include <vector>

#include <Eigen/Dense>

#include <rooms/box_tree.h>
#include <rooms/view_culling.h>
#include <z13_tests/camera_view.h>

namespace z13::station::rooms {
namespace {

constexpr int kBoxes = 600;
constexpr float kSpreadM = 60.f;
constexpr float kBoxSizeM = 1.f;
constexpr unsigned kSeed = 3;
constexpr double kLooseRatio = 4.0;

ViewCulling Looking(const Eigen::Vector3f& eye, const Eigen::Vector3f& target) {
  return {z13::testing::CameraViewFrom(eye, target), std::nullopt};
}

std::vector<Eigen::AlignedBox3f> Scattered(std::mt19937& random) {
  std::uniform_real_distribution<float> place(-kSpreadM, kSpreadM);
  std::vector<Eigen::AlignedBox3f> boxes;
  for (int i = 0; i < kBoxes; ++i) {
    const Eigen::Vector3f centre(place(random), place(random), place(random));
    boxes.emplace_back(centre.array() - kBoxSizeM, centre.array() + kBoxSizeM);
  }
  return boxes;
}

std::vector<uint32_t> Linear(const std::vector<Eigen::AlignedBox3f>& boxes, const ViewCulling& culling) {
  std::vector<uint32_t> visible;
  for (uint32_t i = 0; i < boxes.size(); ++i) {
    if (culling.Visible(boxes[i])) {
      visible.push_back(i);
    }
  }
  return visible;
}

std::vector<uint32_t> Sorted(const BoxTree& tree, const ViewCulling& culling) {
  std::vector<uint32_t> visible;
  tree.Query(culling, visible);
  std::ranges::sort(visible);
  return visible;
}

TEST(BoxTreeTest, FindsTheSameBoxesAsTestingEachOne) {
  std::mt19937 random(kSeed);
  const std::vector<Eigen::AlignedBox3f> boxes = Scattered(random);
  const BoxTree tree(boxes, kLooseRatio);

  for (const Eigen::Vector3f& target : {Eigen::Vector3f(10.f, 0.f, 0.f), Eigen::Vector3f(0.f, -10.f, 3.f),
                                         Eigen::Vector3f(-4.f, 5.f, -1.f)}) {
    const ViewCulling culling = Looking({0.f, 0.f, 0.f}, target);
    const std::vector<uint32_t> expected = Linear(boxes, culling);
    EXPECT_FALSE(expected.empty());
    EXPECT_LT(expected.size(), boxes.size());
    EXPECT_EQ(Sorted(tree, culling), expected);
  }
}

TEST(BoxTreeTest, TakesWholeNodesInViewAndFindsNothingBehindTheCamera) {
  std::mt19937 random(kSeed);
  const std::vector<Eigen::AlignedBox3f> boxes = Scattered(random);
  const BoxTree tree(boxes, kLooseRatio);
  // From far away every box is in the frustum; turned the other way none is.
  const ViewCulling all = Looking({0.f, -4.f * kSpreadM, 0.f}, {0.f, 0.f, 0.f});
  const ViewCulling none = Looking({0.f, -4.f * kSpreadM, 0.f}, {0.f, -8.f * kSpreadM, 0.f});

  EXPECT_EQ(Sorted(tree, all).size(), boxes.size());
  EXPECT_TRUE(Sorted(tree, none).empty());
}

TEST(BoxTreeTest, AnEmptyOrOneBoxTreeWorks) {
  const ViewCulling culling = Looking({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f});
  const BoxTree empty(std::vector<Eigen::AlignedBox3f> {}, kLooseRatio);
  const std::vector<Eigen::AlignedBox3f> one {{Eigen::Vector3f(19.f, -1.f, -1.f), Eigen::Vector3f(21.f, 1.f, 1.f)}};
  const BoxTree single(one, kLooseRatio);

  EXPECT_TRUE(Sorted(empty, culling).empty());
  EXPECT_EQ(Sorted(single, culling), std::vector<uint32_t> {0});
}

TEST(BoxTreeTest, UpdateFollowsBoxesThatMoved) {
  std::mt19937 random(kSeed);
  std::vector<Eigen::AlignedBox3f> boxes = Scattered(random);
  BoxTree tree(boxes, kLooseRatio);
  std::vector<uint32_t> moved;
  for (uint32_t i = 0; i < boxes.size(); i += 5) {
    moved.push_back(i);
  }
  const ViewCulling culling = Looking({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f});

  for (const float shift : {2.f, 9.f, -15.f}) {
    for (const uint32_t id : moved) {
      boxes[id].translate(Eigen::Vector3f(shift, shift / 2.f, 0.f));
    }
    tree.Update(moved, boxes);
    EXPECT_EQ(Sorted(tree, culling), Linear(boxes, culling)) << "shift " << shift;
  }
}

TEST(BoxTreeTest, RebuildsWhenMovingHasLoosenedIt) {
  std::mt19937 random(kSeed);
  std::vector<Eigen::AlignedBox3f> boxes = Scattered(random);
  BoxTree tree(boxes, kLooseRatio);
  std::vector<uint32_t> moved(boxes.size());
  std::iota(moved.begin(), moved.end(), 0U);
  std::uniform_real_distribution<float> fling(-4.f * kSpreadM, 4.f * kSpreadM);
  const ViewCulling culling = Looking({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f});

  EXPECT_EQ(tree.Rebuilds(), 0U);
  for (int step = 0; step < 3; ++step) {
    for (const uint32_t id : moved) {
      boxes[id].translate(Eigen::Vector3f(fling(random), fling(random), fling(random)));
    }
    tree.Update(moved, boxes);
    EXPECT_EQ(Sorted(tree, culling), Linear(boxes, culling));
  }
  EXPECT_GT(tree.Rebuilds(), 0U);
}

TEST(BoxTreeTest, TheLooseRatioSaysWhenMovingRebuilds) {
  constexpr double kTight = 1.01;
  constexpr double kNever = 1e9;
  std::mt19937 random(kSeed);
  std::vector<Eigen::AlignedBox3f> boxes = Scattered(random);
  BoxTree tight(boxes, kTight);
  BoxTree never(boxes, kNever);
  std::vector<uint32_t> moved(boxes.size());
  std::iota(moved.begin(), moved.end(), 0U);
  for (const uint32_t id : moved) {
    boxes[id].translate(Eigen::Vector3f(id % 2 == 0 ? kSpreadM : -kSpreadM, 0.f, 0.f));
  }
  tight.Update(moved, boxes);
  never.Update(moved, boxes);

  EXPECT_GT(tight.Rebuilds(), 0U);
  EXPECT_EQ(never.Rebuilds(), 0U);
}

TEST(BoxTreeTest, ADifferentNumberOfBoxesRebuilds) {
  std::mt19937 random(kSeed);
  std::vector<Eigen::AlignedBox3f> boxes = Scattered(random);
  BoxTree tree(boxes, kLooseRatio);
  boxes.pop_back();
  tree.Update({}, boxes);
  const ViewCulling culling = Looking({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f});

  EXPECT_EQ(tree.Size(), boxes.size());
  EXPECT_EQ(Sorted(tree, culling), Linear(boxes, culling));
}

}  // namespace
}  // namespace z13::station::rooms
