#include "z13.h"

#include <iostream>

#include "core.h"
#include "modules/z13_module/z13_module.h"

namespace z13 {

int Zodiac13::Run(int argc, char *argv[]) {
  std::cout << "Hello from Zodiac 13!!!\n";

  the::Core core(argc, argv);

  core.CreateModule<Z13Module>();

  core.InitModules();
  core.CreateWorld();

  return core.Run();
}

}  // namespace z13
