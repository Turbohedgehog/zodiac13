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

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The air of a room (docs/station-systems-plan.md, "Газ и тепло"), in whole numbers: gases in millimoles, thermal
// energy in millijoules, volumes in cells. The gas is a diatomic ideal one, so its pressure is 2/5 of the thermal
// energy over the volume whatever it is made of, and its temperature the energy over the heat of its molecules.
namespace z13::utility::atmos {

enum class Gas : uint8_t { kOxygen, kNitrogen, kCarbonDioxide };
inline constexpr size_t kGasCount = 3;

inline constexpr int64_t kStandardPascals = 101325;
inline constexpr int64_t kStandardKelvin = 293;
// A millimole of the gas takes this many millijoules to warm by a kelvin (5/2 of the gas constant, rounded).
inline constexpr int64_t kMillijoulesPerMillimoleKelvin = 21;
// The share of the standard air that is oxygen, in thousandths; the rest is nitrogen.
inline constexpr int64_t kOxygenPermille = 210;
inline constexpr int64_t kPermille = 1000;

struct Air {
  std::array<int64_t, kGasCount> millimoles {};
  int64_t energy {};

  int64_t TotalMillimoles() const;
  Air& operator+=(const Air& other);
  Air& operator-=(const Air& other);
  bool operator==(const Air&) const = default;
};

Air operator+(Air first, const Air& second);

// Pascals in `volume_cells` cells (a cell is 1/64 of a cubic meter); none in no volume.
int64_t PressurePascals(const Air& air, int64_t volume_cells);
// Kelvin; none without gas.
int64_t Kelvin(const Air& air);

// `pascals` of standard air at `kelvin` filling `volume_cells`.
Air AirAt(int64_t volume_cells, int64_t pascals, int64_t kelvin);
Air StandardAir(int64_t volume_cells);

// Splits `air` by `weights` (cells shared with each new room): each gets its share, and what rounding leaves goes to the
// heaviest, so nothing is made or lost. All weights zero gives every share nothing.
std::vector<Air> Divide(const Air& air, std::span<const int64_t> weights);

}  // namespace z13::utility::atmos
