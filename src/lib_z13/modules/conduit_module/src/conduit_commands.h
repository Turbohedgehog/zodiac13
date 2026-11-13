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

#include <flecs.h>

namespace z13::conduit {

// conduit_kinds, conduits, lay_conduit and remove_conduit, for the debug channel; edited at once, outside the tick.
void RegisterConduitCommandComponents(flecs::world world);
void RegisterConduitCommandSystems(flecs::world world);

}  // namespace z13::conduit
