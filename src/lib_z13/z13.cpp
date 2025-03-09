#include "z13.h"

#include <iostream>

#include "core.h"
#include "modules/z13_module/z13_module.h"

namespace z13 {

Zodiac13::Zodiac13(int /*argc*/, char* /*argv*/ []) {

}

int Zodiac13::Run() {
  std::cout << "Hello from Zodiac 13!!!\n";

  the::Core core;

  core.CreateModule<Z13Module>();

  core.InitModules();
  core.CreateWorld();

  return core.Run();
}

}  // namespace z13
