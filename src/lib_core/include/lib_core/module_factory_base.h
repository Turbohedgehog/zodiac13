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

// Plugin ABI contract. Deliberately kept as a plain header (not a module):
// the host executable and every plugin DLL compile this definition
// independently, and boost::dll resolves `create_module_factory` across that
// boundary by symbol name + vtable layout. It must not gain module linkage.

#include <memory>
#include <string>

#include <boost/config.hpp>  // BOOST_SYMBOL_VISIBLE (macro-only, safe in a module GMF)

namespace flecs {

struct world;

}  // namespace flecs

namespace z13 {

class BOOST_SYMBOL_VISIBLE ModuleFactoryBase {
 public:
  virtual ~ModuleFactoryBase() = default;
  virtual void RegisterModules(flecs::world& world) = 0;
  virtual const std::string& GetName() const = 0;
};

using ModuleFactoryPtr = std::shared_ptr<ModuleFactoryBase>;

}  // namespace z13
