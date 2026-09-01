#include <iostream>

#include "flex_test.h"

import zodiac13.launcher;

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