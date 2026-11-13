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

#include <memory>

#include <Eigen/Dense>

namespace z13::raylib {

struct GlBlitFunctions;

// Copies a framebuffer to the window's, scaled with a linear filter: rlgl blits with a nearest one.
class GlBlit {
 public:
  // Loads the entry points; needs the GL context current.
  GlBlit();

  // Does nothing if the driver lacks the entry points.
  void ToWindow(unsigned int framebuffer, const Eigen::Vector2i& from, const Eigen::Vector2i& to) const;

 private:
  std::shared_ptr<const GlBlitFunctions> functions_;
};

}  // namespace z13::raylib
