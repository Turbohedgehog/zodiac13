#include <iostream>

#include <z13_launcher/z13_launcher.h>

int main(int argc, char *argv[]) {
  z13::Zodiac13Launcher zodiac13_launcher;

  return zodiac13_launcher.Run(argc, argv);
}