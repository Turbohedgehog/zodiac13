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

#include <physics_tuning_generated.h>

namespace z13 {

// The world's copy of the physics tuning (schemas/fbs/physics_tuning.fbs), a runtime singleton.
struct PhysicsTuning : fbs::physics::PhysicsTuningT {
  using Singleton = void;

  PhysicsTuning() = default;
  explicit PhysicsTuning(const fbs::physics::PhysicsTuningT& values) : fbs::physics::PhysicsTuningT(values) {}
};

}  // namespace z13
