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
#include <boost/config.hpp>
#include <boost/dll/alias.hpp>
#include <lib_core/module_factory_base.h>

extern "C" {

namespace z13 {

class BOOST_SYMBOL_VISIBLE Z13ModuleFactory : public z13::ModuleFactoryBase {
 public:
  static ModuleFactoryPtr CreateFactory();

  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;

  // When false, fully isolates the world from the developer's real on-disk
  // input config: skips both LoadConfig (so a machine that's actually been
  // played on doesn't leak its mouse_sensitivity/bindings into the world)
  // and the SaveConfig write InputConfigLoader otherwise performs the first
  // time no config file exists. Must be called before RegisterModules() runs
  // (i.e. before Core::CreateWorld()).
  void SetUseDiskForInputConfig(bool use_disk);

 private:
  bool use_disk_for_input_config_ {true};
};

}  // namespace z13

BOOST_DLL_ALIAS(
    z13::Z13ModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
