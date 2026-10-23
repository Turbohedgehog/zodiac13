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

#include <lib_core/utils/text_lines.h>

namespace z13 {
namespace {

TEST(TextLinesTest, SplitsOnLineFeedsAndKeepsBlankLines) {
  const std::vector<std::string> lines = SplitLines("a\n\nb\n");

  EXPECT_EQ(lines, (std::vector<std::string> {"a", "", "b"})) << "the last line feed ends b";
}

TEST(TextLinesTest, ALineWithoutAnEndStillCounts) {
  EXPECT_EQ(SplitLines("a\nb"), (std::vector<std::string> {"a", "b"}));
  EXPECT_TRUE(SplitLines("").empty());
}

TEST(TextLinesTest, CarriageReturnsGoWithTheLineEnd) {
  EXPECT_EQ(SplitLines("a\r\nb\r\n"), (std::vector<std::string> {"a", "b"}));
}

TEST(TextLinesTest, JoinsARangeInEitherOrder) {
  const std::vector<std::string> lines {"a", "b", "c", "d"};

  EXPECT_EQ(JoinLines(lines, 1, 2), "b\nc\n");
  EXPECT_EQ(JoinLines(lines, 2, 1), "b\nc\n");
  EXPECT_EQ(JoinLines(lines, 3, 3), "d\n");
}

TEST(TextLinesTest, JoinClampsToTheLinesThereAre) {
  const std::vector<std::string> lines {"a", "b"};

  EXPECT_EQ(JoinLines(lines, 1, 99), "b\n");
  EXPECT_EQ(JoinLines(lines, 50, 99), "b\n");
  EXPECT_TRUE(JoinLines({}, 0, 3).empty());
}

TEST(SelectionAfterTapTest, WithoutASelectionATapSelectsNothing) {
  EXPECT_FALSE(SelectionAfterTap(std::nullopt, 3).has_value());
}

TEST(SelectionAfterTapTest, ATapOutsideGrowsTheSelectionToThatLine) {
  const std::optional<LineSelection> selection = LineSelection {.first = 4, .last = 6};

  const auto below = SelectionAfterTap(selection, 9);
  ASSERT_TRUE(below.has_value());
  EXPECT_EQ(below->first, 4U);
  EXPECT_EQ(below->last, 9U);
  const auto above = SelectionAfterTap(selection, 1);
  ASSERT_TRUE(above.has_value());
  EXPECT_EQ(above->first, 1U);
  EXPECT_EQ(above->last, 6U);
}

TEST(SelectionAfterTapTest, GrowingWorksWhenTheSelectionWasMadeUpwards) {
  const std::optional<LineSelection> selection = LineSelection {.first = 6, .last = 4};

  const auto grown = SelectionAfterTap(selection, 8);
  ASSERT_TRUE(grown.has_value());
  EXPECT_EQ(grown->first, 4U);
  EXPECT_EQ(grown->last, 8U);
}

TEST(SelectionAfterTapTest, ATapInsideNarrowsToThatLineAndAnotherTapClears) {
  const std::optional<LineSelection> selection = LineSelection {.first = 4, .last = 6};

  const auto narrowed = SelectionAfterTap(selection, 5);
  ASSERT_TRUE(narrowed.has_value());
  EXPECT_EQ(narrowed->first, 5U);
  EXPECT_EQ(narrowed->last, 5U);
  EXPECT_FALSE(SelectionAfterTap(narrowed, 5).has_value());
}

TEST(SelectionAfterTapTest, ATapOnOtherLineGrowsASingleLineSelection) {
  const auto grown = SelectionAfterTap(LineSelection {.first = 5, .last = 5}, 7);

  ASSERT_TRUE(grown.has_value());
  EXPECT_EQ(grown->first, 5U);
  EXPECT_EQ(grown->last, 7U);
}

}  // namespace
}  // namespace z13
