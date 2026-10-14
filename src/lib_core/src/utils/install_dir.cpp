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

#include <lib_core/utils/install_dir.h>

#include <format>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/dll/shared_library.hpp>

namespace z13 {

std::filesystem::path InstallDir() {
#if defined(__ANDROID__)
  return std::filesystem::current_path();
#else
  return boost::dll::program_location().parent_path().string();
#endif
}

std::filesystem::path ModuleLibraryPath(const std::filesystem::path& entry) {
#if defined(__ANDROID__)
  // Android installs every library of the APK flat into one directory, this one's included.
  const std::filesystem::path library_dir = boost::dll::this_line_location().parent_path().string();
  return library_dir / std::format("lib{}.so", entry.filename().string());
#else
  auto path = std::filesystem::absolute(InstallDir() / entry);
  path.replace_extension(boost::dll::shared_library::suffix().native());
  return path;
#endif
}

}  // namespace z13
