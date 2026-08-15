// z13.test_dll module interface unit.
// Внутренний модуль DLL test_dll_module (план миграции, раздел 4.5).

module;

export module z13.test_dll;

import z13.core;

export namespace z13::dll {

class TestDllModule {
 public:
  TestDllModule(flecs::world& world);
};

class TestDllSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::dll