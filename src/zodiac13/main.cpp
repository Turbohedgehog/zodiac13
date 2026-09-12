#include <iostream>

#include <z13_launcher/z13_launcher.h>

#include "flex_test.h"

#ifdef _WIN32
// Hybrid-graphics laptops: ask the NVIDIA / AMD driver to run this process on the
// discrete GPU. Must live in the executable — a later-loaded DLL is too late.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char *argv[]) {
  // TestObservers();
  // TestObserverWithSingleton();

  z13::Zodiac13Launcher zodiac13_launcher;

  // test_emit_2();
  // test_flex2(argc, argv);
  // TestVariant();

  // test_component_lifetime();

  return zodiac13_launcher.Run(argc, argv);
}