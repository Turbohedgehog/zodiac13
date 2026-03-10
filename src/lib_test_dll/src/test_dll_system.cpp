#include "test_dll_system.h"

#include <flecs.h>
#include <lib_core/log.h>

namespace z13::dll {

void TestDllSystem::Register(flecs::world& world) {
  LOG_INFO("~~~~~ TestDllSystem::Register");
}

}  // namespace z13::dll