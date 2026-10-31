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

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <z13/components/rooms.h>

namespace z13::station {

// A debug layer a module publishes (power, gas, ...): coloured boxes in cells and a line of text. The renderer draws
// whichever layer is shown and steps through them, knowing nothing about what they show.
struct OverlayLayer {
  std::string name;
  std::vector<OverlayBox> boxes;
  std::string label;
};

// Every layer registered, in registration order, and the one on. A module fills its layer only while it is shown.
struct OverlayLayers {
  using Singleton = void;
  std::vector<OverlayLayer> layers;
  std::optional<std::string> shown;

  OverlayLayer* Find(const std::string& name) {
    const auto found = std::ranges::find(layers, name, &OverlayLayer::name);
    return found != layers.end() ? &*found : nullptr;
  }
  const OverlayLayer* Find(const std::string& name) const {
    const auto found = std::ranges::find(layers, name, &OverlayLayer::name);
    return found != layers.end() ? &*found : nullptr;
  }
  bool IsShown(const std::string& name) const { return shown == name; }
  // Off, then each layer in turn, then off again.
  void Step() {
    if (layers.empty()) {
      shown.reset();
      return;
    }
    const auto current = shown ? std::ranges::find(layers, *shown, &OverlayLayer::name) : layers.end();
    if (!shown) {
      shown = layers.front().name;
    } else if (current == layers.end() || std::next(current) == layers.end()) {
      shown.reset();
    } else {
      shown = std::next(current)->name;
    }
  }
};

}  // namespace z13::station
