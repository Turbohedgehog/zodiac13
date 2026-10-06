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

#include <building_tuning_generated.h>

namespace z13 {

// The world's copy of the building tuning (schemas/fbs/building_tuning.fbs), a runtime singleton.
struct BuildingTuning : fbs::building::BuildingTuningT {
  using Singleton = void;

  BuildingTuning() = default;
  explicit BuildingTuning(const fbs::building::BuildingTuningT& values) : fbs::building::BuildingTuningT(values) {}
};

}  // namespace z13
