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

module;

#include <filesystem>
#include <memory>

#include <lib_core/module_factory_base.h>

// Internal partition: exported names are visible to other units of this module
// that import it, but the primary interface unit does NOT re-export it, so it
// stays invisible to external `import zodiac13.core;` consumers. boost::dll is
// kept in the implementation (pimpl) so this BMI stays light.
export module zodiac13.core:module_lib_holder;

export namespace z13 {

class ModuleLibHolder {
 public:
  ModuleLibHolder();
  ~ModuleLibHolder();

  ModuleFactoryPtr AppendModuleLib(std::filesystem::path lib_path, bool append_platform_extension = true);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace z13
