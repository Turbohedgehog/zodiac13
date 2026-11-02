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

#include <atmos/air.h>

namespace z13::utility::atmos {

struct Breath {
  // Oxygen taken and carbon dioxide given back, in millimoles.
  int64_t millimoles {};
  // The body's heat given to the air, in millijoules.
  int64_t heat {};
};

// A body breathing in a room for a step. False when the air had less oxygen than the breath wanted (what there was is
// taken).
bool Breathe(Air& air, const Breath& breath);

// What an atmosphere unit holds a room at, and the most it does to the room in one step.
struct Regulation {
  int64_t pascals {};
  int64_t kelvin {};
  // Carbon dioxide taken out, standard air put in to the target pressure, heat put in or taken out to the target
  // temperature.
  int64_t scrub_millimoles {};
  int64_t fill_millimoles {};
  int64_t heat_millijoules {};
};

// One step of a unit in a room of `volume_cells` cells. The unit makes and unmakes what it needs: the gas it puts in
// and the heat come from nowhere, and the carbon dioxide it takes goes nowhere.
void Regulate(Air& air, int64_t volume_cells, const Regulation& regulation);

}  // namespace z13::utility::atmos
