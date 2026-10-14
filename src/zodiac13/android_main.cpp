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
#include <expected>
#include <filesystem>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/spdlog.h>

#include <lib_core/utils/file_io.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/status.h>
#include <z13_launcher/z13_launcher.h>

namespace {

// Written by cmake/android_assets.cmake: a hash of the files, then one path per line.
constexpr std::string_view kManifest = "z13_files.txt";
constexpr std::string_view kLogTag = "zodiac13";
const std::filesystem::path kInstallDir = "install";
const std::filesystem::path kDataDir = "data";

std::expected<std::string, std::string> ReadApkFile(const std::string& path) {
  size_t size {};
  void* data = SDL_LoadFile(path.c_str(), &size);
  if (data == nullptr) {
    return std::unexpected(std::format("cannot read '{}' from the APK: {}", path, SDL_GetError()));
  }
  std::string contents(static_cast<const char*>(data), size);
  SDL_free(data);
  return contents;
}

// The code reads config/ and assets/ through std::filesystem, which can't see into the APK:
// copies them into `dir` unless the copy there has the same manifest.
z13::Status UnpackApkFiles(const std::filesystem::path& dir) {
  const auto manifest = ReadApkFile(std::string(kManifest));
  if (!manifest) {
    return std::unexpected(manifest.error());
  }
  const std::filesystem::path manifest_copy = dir / kManifest;
  if (const auto unpacked = z13::ReadFile(manifest_copy); unpacked && *unpacked == *manifest) {
    return {};
  }

  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::istringstream lines(*manifest);
  std::string path;
  std::getline(lines, path);  // the hash
  while (std::getline(lines, path)) {
    const auto contents = ReadApkFile(path);
    if (!contents) {
      return std::unexpected(contents.error());
    }
    if (const auto written = z13::WriteFile(dir / path, *contents); !written) {
      return written;
    }
  }
  // Last, so that an interrupted unpack is redone on the next start.
  return z13::WriteFile(manifest_copy, *manifest);
}

}  // namespace

int main(int argc, char* argv[]) {
  spdlog::set_default_logger(spdlog::android_logger_mt(std::string(kLogTag), std::string(kLogTag)));

  const char* storage = SDL_GetAndroidInternalStoragePath();
  if (storage == nullptr) {
    z13::log_error("no internal storage: {}", SDL_GetError());
    return EXIT_FAILURE;
  }
  const std::filesystem::path internal_storage = storage;
  const std::filesystem::path install_dir = internal_storage / kInstallDir;
  if (const auto unpacked = UnpackApkFiles(install_dir); !unpacked) {
    z13::log_error("{}", unpacked.error());
    return EXIT_FAILURE;
  }
  // InstallDir() on Android.
  std::error_code error;
  std::filesystem::current_path(install_dir, error);
  if (error) {
    z13::log_error("cannot enter '{}': {}", install_dir.string(), error.message());
    return EXIT_FAILURE;
  }
  // Android has no home directory: settings and saves go where sago::getDataHome() looks first.
  setenv("XDG_DATA_HOME", (internal_storage / kDataDir).c_str(), 1);

  z13::Zodiac13Launcher launcher;
  return launcher.Run(argc, argv);
}
