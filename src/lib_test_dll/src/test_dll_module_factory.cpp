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

#include <lib_test_dll/test_dll_module_factory.h>

#include <flecs.h>

#include <lib_core/log.h>

#include "test_dll_module.h"

namespace z13::dll {

ModuleFactoryPtr TestDllModuleFactory::CreateFactory() {
  return std::make_shared<TestDllModuleFactory>();
}

TestDllModuleFactory::~TestDllModuleFactory() {
  LOG_INFO("~~~~~ TestDllModuleFactory::~TestDllModuleFactory");
}

void TestDllModuleFactory::RegisterModules(flecs::world& world) {
  world.import<z13::dll::TestDllModule>();
}

const std::string& TestDllModuleFactory::GetName() const {
  static std::string name = "TestDllModuleFactory";

  return name;
}

}  // namespace z13::dll
