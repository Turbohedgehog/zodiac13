#include "test_dll_module.h"

#include "test_dll_system.h"

namespace z13::dll {

TestDllModule::TestDllModule(flecs::world& world) {
  TestDllSystem::Register(world);
}

}  // namespace z13::dll
