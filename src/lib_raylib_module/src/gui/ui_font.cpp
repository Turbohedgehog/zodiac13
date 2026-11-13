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


#include "ui_font.h"

#include <filesystem>
#include <utility>

#include <imgui.h>

namespace z13::raylib::gui {

namespace {

const std::filesystem::path kUiFontPath = "fonts/DejaVuSans.ttf";
// Close to the size of the default font it replaces; the display scale multiplies it (GuiSystem).
constexpr float kUiFontSize = 15.f;

}  // namespace

Status AddUiFont(const AssetSource& assets, std::string& data) {
  auto bytes = assets.ReadAsset(kUiFontPath);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  data = std::move(*bytes);
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;
  if (ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), kUiFontSize, &config) ==
      nullptr) {
    return std::unexpected("ImGui cannot read the interface font");
  }
  return {};
}

}  // namespace z13::raylib::gui
