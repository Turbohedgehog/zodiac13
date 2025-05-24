#include <iostream>

#include <z13_launcher/z13_launcher.h>

#include "flex_test.h"

int main(int argc, char *argv[]) {
  z13::Zodiac13Launcher zodiac13_launcher;

  test_flex2(argc, argv);

  return zodiac13_launcher.Run(argc, argv);
}