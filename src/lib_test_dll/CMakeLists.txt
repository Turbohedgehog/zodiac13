project(test_dll_module)

find_package(Boost REQUIRED COMPONENTS dll)

z13_add_plugin(
  ${PROJECT_NAME} modules/test_dll
  SOURCES
  src/test_dll_module.cpp
  src/test_dll_module_factory.cpp
  src/test_dll_system.cpp
  PRIVATE
  zodiac13::core
  Boost::dll
)
