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

#include <lib_core/utils/statistics.h>

namespace z13 {
namespace {

constexpr double kTolerance = 1e-9;

TEST(StatisticsTest, MeanOfValuesAndOfNone) {
  const std::vector<double> values {1.0, 2.0, 6.0};

  EXPECT_NEAR(Mean(values), 3.0, kTolerance);
  EXPECT_NEAR(Mean({}), 0.0, kTolerance);
}

TEST(StatisticsTest, MedianOfAnOddCountIsTheMiddleOne) {
  EXPECT_NEAR(Median({9.0, 1.0, 5.0}), 5.0, kTolerance) << "sorted first";
}

TEST(StatisticsTest, MedianOfAnEvenCountIsTheMeanOfTheMiddleTwo) {
  EXPECT_NEAR(Median({4.0, 1.0, 3.0, 10.0}), 3.5, kTolerance);
}

TEST(StatisticsTest, MedianOfNoneIsZero) {
  EXPECT_NEAR(Median({}), 0.0, kTolerance);
}

}  // namespace
}  // namespace z13
