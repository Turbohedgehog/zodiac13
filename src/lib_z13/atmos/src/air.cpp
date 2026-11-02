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


#include <atmos/air.h>

#include <algorithm>
#include <numeric>

namespace z13::utility::atmos {

namespace {

// 2/5 of the energy in joules over the volume in cubic meters, as a fraction of millijoules and cells.
constexpr int64_t kPressureNumerator = 16;
constexpr int64_t kPressureDenominator = 625;

}  // namespace

int64_t Air::TotalMillimoles() const {
  return std::accumulate(millimoles.begin(), millimoles.end(), int64_t {});
}

Air& Air::operator+=(const Air& other) {
  for (size_t gas = 0; gas < kGasCount; ++gas) {
    millimoles[gas] += other.millimoles[gas];
  }
  energy += other.energy;
  return *this;
}

Air& Air::operator-=(const Air& other) {
  for (size_t gas = 0; gas < kGasCount; ++gas) {
    millimoles[gas] -= other.millimoles[gas];
  }
  energy -= other.energy;
  return *this;
}

Air operator+(Air first, const Air& second) {
  return first += second;
}

int64_t PressurePascals(const Air& air, int64_t volume_cells) {
  return volume_cells > 0 ? (air.energy * kPressureNumerator) / (kPressureDenominator * volume_cells) : 0;
}

int64_t Kelvin(const Air& air) {
  const int64_t molecules = air.TotalMillimoles();
  return molecules > 0 ? air.energy / (kMillijoulesPerMillimoleKelvin * molecules) : 0;
}

Air AirAt(int64_t volume_cells, int64_t pascals, int64_t kelvin) {
  Air air;
  air.energy = (pascals * kPressureDenominator * volume_cells) / kPressureNumerator;
  const int64_t molecules = kelvin > 0 ? air.energy / (kMillijoulesPerMillimoleKelvin * kelvin) : 0;
  const int64_t oxygen = molecules * kOxygenPermille / kPermille;
  air.millimoles[static_cast<size_t>(Gas::kOxygen)] = oxygen;
  air.millimoles[static_cast<size_t>(Gas::kNitrogen)] = molecules - oxygen;
  return air;
}

Air StandardAir(int64_t volume_cells) {
  return AirAt(volume_cells, kStandardPascals, kStandardKelvin);
}

std::vector<Air> Divide(const Air& air, std::span<const int64_t> weights) {
  std::vector<Air> shares(weights.size());
  const int64_t whole = std::accumulate(weights.begin(), weights.end(), int64_t {});
  if (whole <= 0) {
    return shares;
  }
  const auto heaviest = static_cast<size_t>(std::ranges::max_element(weights) - weights.begin());
  const auto split = [&](int64_t amount, auto member) {
    int64_t given {};
    for (size_t i = 0; i < weights.size(); ++i) {
      const int64_t share = amount * weights[i] / whole;
      member(shares[i]) = share;
      given += share;
    }
    member(shares[heaviest]) += amount - given;
  };
  for (size_t gas = 0; gas < kGasCount; ++gas) {
    split(air.millimoles[gas], [gas](Air& target) -> int64_t& { return target.millimoles[gas]; });
  }
  split(air.energy, [](Air& target) -> int64_t& { return target.energy; });
  return shares;
}

}  // namespace z13::utility::atmos
