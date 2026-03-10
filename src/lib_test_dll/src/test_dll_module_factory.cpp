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
