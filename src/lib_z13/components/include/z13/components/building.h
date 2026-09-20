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

namespace z13::building {

// Edge length of a placed block's collision/visual cube, shared between the
// building module (placement/removal overlap tests) and the render module
// (brush preview + placed-block mesh) so they can't drift apart.
inline constexpr float kBlockSize = 0.6f;

struct BuildingTool {
  using State = void;
};

struct Brush {
  float distance {};
};

struct BasicBlock {
  using State = void;
};

struct RequestBuildBlock {};
struct RequestDestroyBlock {};

}  // namespace z13::building
