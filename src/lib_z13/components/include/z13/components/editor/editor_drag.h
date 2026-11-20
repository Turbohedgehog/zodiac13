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

#include <string>

#include <Eigen/Dense>

namespace z13::editor {

// A room dragged with the left button; `offset_cells` is how far the grip moved. Client only, never state.
struct EditorDrag {
  using Singleton = void;

  bool pressed {};
  Eigen::Vector2f press_pixel = Eigen::Vector2f::Zero();
  bool active {};
  std::string room;
  // Per axis, 1 where that side follows the pointer: both for a move, one for an edge.
  Eigen::Vector2i low_follows = Eigen::Vector2i::Ones();
  Eigen::Vector2i high_follows = Eigen::Vector2i::Ones();
  Eigen::Vector2f grab_cells = Eigen::Vector2f::Zero();
  Eigen::Vector2i offset_cells = Eigen::Vector2i::Zero();
};

}  // namespace z13::editor
