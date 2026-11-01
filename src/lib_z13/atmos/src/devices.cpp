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


#include <atmos/devices.h>

#include <algorithm>

namespace z13::utility::atmos {

namespace {

constexpr size_t kOxygen = static_cast<size_t>(Gas::kOxygen);
constexpr size_t kNitrogen = static_cast<size_t>(Gas::kNitrogen);
constexpr size_t kCarbonDioxide = static_cast<size_t>(Gas::kCarbonDioxide);

}  // namespace

bool Breathe(Air& air, const Breath& breath) {
  const int64_t taken = std::min(air.millimoles[kOxygen], breath.millimoles);
  air.millimoles[kOxygen] -= taken;
  air.millimoles[kCarbonDioxide] += taken;
  air.energy += breath.heat;
  return taken == breath.millimoles;
}

void Regulate(Air& air, int64_t volume_cells, const Regulation& regulation) {
  // Scrubbing takes its share of the heat with the gas, so the temperature holds.
  const int64_t before = air.TotalMillimoles();
  const int64_t scrubbed = std::min(air.millimoles[kCarbonDioxide], regulation.scrub_millimoles);
  if (scrubbed > 0) {
    air.energy -= air.energy * scrubbed / before;
    air.millimoles[kCarbonDioxide] -= scrubbed;
  }

  // Filling puts standard air in at the target temperature, so the temperature moves towards it with the gas.
  const int64_t wanted = AirAt(volume_cells, regulation.pascals, regulation.kelvin).TotalMillimoles();
  const int64_t added = std::clamp(wanted - air.TotalMillimoles(), int64_t {}, regulation.fill_millimoles);
  if (added > 0) {
    const int64_t oxygen = added * kOxygenPermille / kPermille;
    air.millimoles[kOxygen] += oxygen;
    air.millimoles[kNitrogen] += added - oxygen;
    air.energy += added * kMillijoulesPerMillimoleKelvin * regulation.kelvin;
  }

  const int64_t molecules = air.TotalMillimoles();
  if (molecules > 0) {
    const int64_t per_kelvin = molecules * kMillijoulesPerMillimoleKelvin;
    const int64_t gap = regulation.kelvin * per_kelvin - air.energy;
    air.energy += std::clamp(gap, -regulation.heat_millijoules, regulation.heat_millijoules);
  }
}

}  // namespace z13::utility::atmos
