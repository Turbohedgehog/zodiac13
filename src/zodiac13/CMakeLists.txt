cmake_minimum_required(VERSION 3.13)

project(zodiac13)

set(CMAKE_CXX_STANDARD 20)

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  #${CORE_INCLUDE_DIR}
  ${Z13_INCLUDE_DIR}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  # ${Z13_CORE_LIB}
  ${Z13_LIB}
)

install(TARGETS ${PROJECT_NAME} DESTINATION ${ZODIAC_13_ROOT_DIR}/bin)