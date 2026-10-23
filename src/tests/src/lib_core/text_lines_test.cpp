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

}  // namespace
}  // namespace z13
