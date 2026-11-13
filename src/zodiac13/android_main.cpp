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

// The app's entry point on Android: SDLActivity loads libmain.so and calls SDL_main.

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/spdlog.h>

#include <lib_core/utils/asset_pack.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/log.h>
#include <z13_launcher/z13_launcher.h>

#include "apk_pack_reader.h"

namespace {

constexpr std::string_view kLogTag = "zodiac13";
const std::filesystem::path kDataDir = "data";

}  // namespace

int main(int argc, char* argv[]) {
  spdlog::set_default_logger(spdlog::android_logger_mt(std::string(kLogTag), std::string(kLogTag)));

  const char* storage = SDL_GetAndroidInternalStoragePath();
  if (storage == nullptr) {
    z13::log_error("no internal storage: {}", SDL_GetError());
    return EXIT_FAILURE;
  }
  const std::filesystem::path internal_storage = storage;
  // Android has no home directory: settings and saves go where sago::getDataHome() looks first.
  setenv("XDG_DATA_HOME", (internal_storage / kDataDir).c_str(), 1);

  auto pack = z13::AssetPack::Open(std::make_unique<z13::ApkPackReader>(std::string(z13::kAssetPackFile)));
  if (!pack) {
    z13::log_error("{}", pack.error());
    return EXIT_FAILURE;
  }
  z13::Zodiac13Launcher launcher;
  launcher.UseAssets(z13::AssetSource(std::make_shared<const z13::AssetPack>(*std::move(pack))));
  return launcher.Run(argc, argv);
}
