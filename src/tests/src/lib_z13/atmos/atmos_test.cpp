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

#include <array>
#include <vector>

#include <atmos/air.h>
#include <atmos/devices.h>
#include <atmos/exchange.h>

namespace z13::utility::atmos {
namespace {

// Rounding in the pressure and the temperature of an air built from them: the integer divisions on the way.
constexpr int64_t kPascalsTolerance = 2;
constexpr int64_t kKelvinTolerance = 1;
// The flow and the regulation settle to within a percent of the standard pressure, and a unit within a couple of kelvin.
constexpr int64_t kSettledPascals = kStandardPascals / 100;
constexpr int64_t kSettledKelvin = 2;

constexpr int64_t kRoomCells = 1200;
constexpr double kTwiceTheVolume = 2.0;
constexpr size_t kOxygen = static_cast<size_t>(Gas::kOxygen);
constexpr size_t kNitrogen = static_cast<size_t>(Gas::kNitrogen);
constexpr size_t kCarbonDioxide = static_cast<size_t>(Gas::kCarbonDioxide);
// A door's opening, 4 by 8 cells.
constexpr int64_t kDoorCells = 32;
// The shipped tuning of atmos_tuning.fbs.
constexpr ExchangeSettings kSettings {.coefficient = 10'000, .max_outflow_ppm = 500'000};

Air Total(std::span<const Chamber> chambers) {
  Air total;
  for (const Chamber& chamber : chambers) {
    total += chamber.air;
  }
  return total;
}

TEST(AirTest, StandardAirHoldsItsPressureTemperatureAndShareOfOxygen) {
  const Air air = StandardAir(kRoomCells);

  EXPECT_NEAR(static_cast<double>(PressurePascals(air, kRoomCells)), static_cast<double>(kStandardPascals),
              static_cast<double>(kPascalsTolerance));
  EXPECT_NEAR(static_cast<double>(Kelvin(air)), static_cast<double>(kStandardKelvin),
              static_cast<double>(kKelvinTolerance));
  EXPECT_EQ(air.millimoles[kOxygen], air.TotalMillimoles() * kOxygenPermille / kPermille);
  EXPECT_EQ(air.millimoles[kCarbonDioxide], 0);
}

TEST(AirTest, NothingHasNoPressureOrTemperature) {
  EXPECT_EQ(PressurePascals(Air {}, kRoomCells), 0);
  EXPECT_EQ(PressurePascals(StandardAir(kRoomCells), 0), 0);
  EXPECT_EQ(Kelvin(Air {}), 0);
}

TEST(AirTest, ThePressureOfTheSameAirHalvesInTwiceTheVolume) {
  const Air air = StandardAir(kRoomCells);

  EXPECT_NEAR(static_cast<double>(PressurePascals(air, 2 * kRoomCells)), static_cast<double>(kStandardPascals) / kTwiceTheVolume,
              static_cast<double>(kPascalsTolerance));
}

TEST(AirTest, DivideKeepsEveryMillimoleAndMillijoule) {
  Air air = StandardAir(kRoomCells);
  air.millimoles[kCarbonDioxide] = 7;
  const std::array<int64_t, 4> weights = {3, 5, 11, 1};

  const std::vector<Air> shares = Divide(air, weights);

  Air sum;
  for (const Air& share : shares) {
    sum += share;
  }
  EXPECT_EQ(sum, air);
  EXPECT_GT(shares[2].energy, shares[1].energy) << "by the weights";
}

TEST(AirTest, DivideByNoWeightGivesNothing) {
  const std::array<int64_t, 2> weights = {0, 0};

  for (const Air& share : Divide(StandardAir(kRoomCells), weights)) {
    EXPECT_EQ(share, Air {});
  }
}

TEST(ExchangeTest, AirGoesFromTheHigherPressureToTheLowerAndNothingIsMadeOrLost) {
  std::vector<Chamber> chambers = {{.air = StandardAir(kRoomCells), .volume_cells = kRoomCells},
                                   {.air = Air {}, .volume_cells = kRoomCells}};
  const std::vector<Link> links = {{.a = 0, .b = 1, .area_cells = kDoorCells}};
  const Air before = Total(chambers);

  const Air lost = Exchange(chambers, links, kSettings);

  EXPECT_LT(chambers[0].air.energy, before.energy);
  EXPECT_GT(chambers[1].air.energy, 0);
  EXPECT_EQ(Total(chambers) + lost, before);
  EXPECT_EQ(lost, Air {});
}

TEST(ExchangeTest, TwoRoomsLevelWithoutTheirPressuresCrossing) {
  std::vector<Chamber> chambers = {{.air = StandardAir(kRoomCells), .volume_cells = kRoomCells},
                                   {.air = Air {}, .volume_cells = 3 * kRoomCells}};
  const std::vector<Link> links = {{.a = 0, .b = 1, .area_cells = kDoorCells}};
  constexpr int kSteps = 200;

  for (int step = 0; step < kSteps; ++step) {
    Exchange(chambers, links, kSettings);
    ASSERT_GE(PressurePascals(chambers[0].air, kRoomCells), PressurePascals(chambers[1].air, 3 * kRoomCells))
        << "step " << step;
  }

  EXPECT_NEAR(static_cast<double>(PressurePascals(chambers[0].air, kRoomCells)),
              static_cast<double>(PressurePascals(chambers[1].air, 3 * kRoomCells)),
              static_cast<double>(kSettledPascals));
}

TEST(ExchangeTest, ARoomOpenToTheVacuumEmptiesAndWhatLeavesIsCounted) {
  std::vector<Chamber> chambers = {{.air = StandardAir(kRoomCells), .volume_cells = kRoomCells}};
  const std::vector<Link> links = {{.a = 0, .b = kOutside, .area_cells = kDoorCells}};
  const Air before = chambers[0].air;
  Air lost;
  constexpr int kSteps = 60;

  for (int step = 0; step < kSteps; ++step) {
    lost += Exchange(chambers, links, kSettings);
  }

  EXPECT_LT(PressurePascals(chambers[0].air, kRoomCells), kStandardPascals / 100);
  EXPECT_EQ(chambers[0].air + lost, before);
}

TEST(ExchangeTest, NoLinkChangesNothing) {
  std::vector<Chamber> chambers = {{.air = StandardAir(kRoomCells), .volume_cells = kRoomCells}};
  const Air before = chambers[0].air;

  EXPECT_EQ(Exchange(chambers, {}, kSettings), Air {});
  EXPECT_EQ(chambers[0].air, before);
}

TEST(ExchangeTest, ARoomWithManyOpeningsGivesNoMoreThanItsCapAtOneStep) {
  std::vector<Chamber> chambers = {{.air = StandardAir(kRoomCells), .volume_cells = kRoomCells}};
  const std::vector<Link> links(8, Link {.a = 0, .b = kOutside, .area_cells = 10 * kDoorCells});
  const Air before = chambers[0].air;

  Exchange(chambers, links, kSettings);

  EXPECT_GE(chambers[0].air.energy, before.energy - before.energy * kSettings.max_outflow_ppm / kPartsPerMillion - 8);
  for (size_t gas = 0; gas < kGasCount; ++gas) {
    EXPECT_GE(chambers[0].air.millimoles[gas], 0);
  }
}

TEST(ExchangeTest, TheOrderOfTheLinksDoesNotChangeTheOutcome) {
  const auto run = [](std::vector<Link> links) {
    std::vector<Chamber> chambers = {{.air = StandardAir(kRoomCells), .volume_cells = kRoomCells},
                                     {.air = Air {}, .volume_cells = kRoomCells},
                                     {.air = StandardAir(kRoomCells / 2), .volume_cells = kRoomCells}};
    Exchange(chambers, links, kSettings);
    return std::vector {chambers[0].air, chambers[1].air, chambers[2].air};
  };
  const Link first {.a = 0, .b = 1, .area_cells = kDoorCells};
  const Link second {.a = 2, .b = 1, .area_cells = kDoorCells};

  EXPECT_EQ(run({first, second}), run({second, first}));
}

TEST(BreathTest, TakesOxygenAndGivesCarbonDioxideAndHeat) {
  Air air = StandardAir(kRoomCells);
  const Air before = air;
  constexpr Breath kBreath {.millimoles = 5, .heat = 700};

  EXPECT_TRUE(Breathe(air, kBreath));

  EXPECT_EQ(air.millimoles[kOxygen], before.millimoles[kOxygen] - 5);
  EXPECT_EQ(air.millimoles[kCarbonDioxide], 5);
  EXPECT_EQ(air.energy, before.energy + 700);
  EXPECT_EQ(air.millimoles[kNitrogen], before.millimoles[kNitrogen]);
}

TEST(BreathTest, WithTooLittleOxygenItTakesWhatThereIsAndSaysSo) {
  Air air;
  air.millimoles[kOxygen] = 2;

  EXPECT_FALSE(Breathe(air, {.millimoles = 5}));

  EXPECT_EQ(air.millimoles[kOxygen], 0);
  EXPECT_EQ(air.millimoles[kCarbonDioxide], 2);
}

constexpr Regulation kHold {.pascals = kStandardPascals,
                            .kelvin = kStandardKelvin,
                            .scrub_millimoles = 100,
                            .fill_millimoles = 5000,
                            .heat_millijoules = 5'000'000};

TEST(RegulateTest, ScrubsCarbonDioxideAtItsRate) {
  Air air = StandardAir(kRoomCells);
  air.millimoles[kCarbonDioxide] = 250;

  Regulate(air, kRoomCells, kHold);

  EXPECT_EQ(air.millimoles[kCarbonDioxide], 150);
}

TEST(RegulateTest, FillsAnEmptyRoomToTheTargetPressureWithoutPassingIt) {
  Air air;
  constexpr int kSteps = 400;

  for (int step = 0; step < kSteps; ++step) {
    Regulate(air, kRoomCells, kHold);
    ASSERT_LE(PressurePascals(air, kRoomCells), kStandardPascals + kPascalsTolerance) << step;
  }

  EXPECT_NEAR(static_cast<double>(PressurePascals(air, kRoomCells)), static_cast<double>(kStandardPascals),
              static_cast<double>(kSettledPascals));
  EXPECT_GT(air.millimoles[kOxygen], 0);
}

TEST(RegulateTest, WarmsAColdRoomAndCoolsAHotOne) {
  Air cold = AirAt(kRoomCells, kStandardPascals, kStandardKelvin - 30);
  Air hot = AirAt(kRoomCells, kStandardPascals, kStandardKelvin + 30);
  constexpr int kSteps = 400;

  for (int step = 0; step < kSteps; ++step) {
    Regulate(cold, kRoomCells, kHold);
    Regulate(hot, kRoomCells, kHold);
  }

  EXPECT_NEAR(static_cast<double>(Kelvin(cold)), static_cast<double>(kStandardKelvin),
              static_cast<double>(kSettledKelvin));
  EXPECT_NEAR(static_cast<double>(Kelvin(hot)), static_cast<double>(kStandardKelvin),
              static_cast<double>(kSettledKelvin));
}

}  // namespace
}  // namespace z13::utility::atmos
