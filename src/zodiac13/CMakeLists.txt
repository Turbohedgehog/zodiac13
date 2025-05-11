cmake_minimum_required(VERSION 3.21)

project(zodiac13)

set(CMAKE_CXX_STANDARD 20)

add_executable(${PROJECT_NAME} main.cpp)

# message(STATUS "Output dir: $<TARGET_FILE_DIR:zodiac13>")

# target_include_directories(
#   ${PROJECT_NAME}
#   PRIVATE
#   ${Z13_INCLUDE_DIR}
# )

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::z13_launcher
  # ${Z13_LIB}
)

# get_target_property(EXE_PATH ${PROJECT_NAME} RUNTIME_OUTPUT_DIRECTORY)

# message("~~~ CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
# message("~~~ PROJECT_SOURCE_DIR = ${PROJECT_SOURCE_DIR}")

# get_target_property(APP_OUTPUT_DIR ${PROJECT_NAME} RUNTIME_OUTPUT_DIRECTORY)
# message(STATUS "~~~~ Директория my_app: ${APP_OUTPUT_DIR}")

# Или с помощью генераторных выражений (например, для установки)
# message(STATUS "~~~~~ Полный путь к my_app: $<TARGET_FILE:zodiac13>")

# Копируем все из этой директории
# install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
#     DESTINATION ${PROJECT_SOURCE_DIR}/bin
#     # FILES_MATCHING
#     # PATTERN "*"
# )

# set (Z13_target $<TARGET_FILE_DIR:zodiac13>)
# message("~~~ CMAKE_CURRENT_BINARY_DIR = ${CMAKE_CURRENT_BINARY_DIR}")

# get_target_property(APP_OUTPUT_DIR zodiac13 RUNTIME_OUTPUT_DIRECTORY)
# message(STATUS "Директория zodiac13: ${APP_OUTPUT_DIR}")

message("~~~ CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message("~~~ CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("~~~ RUNTIME_OUTPUT_DIRECTORY = ${RUNTIME_OUTPUT_DIRECTORY}")

set_target_properties(${PROJECT_NAME} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${ZODIAC_13_ROOT_DIR}/bin
)

# install(TARGETS ${PROJECT_NAME}
#         # RUNTIME_DEPENDENCY_SET runtime_deps
#         DESTINATION ${ZODIAC_13_ROOT_DIR}/bin)

# install(RUNTIME_DEPENDENCY_SET runtime_deps
#     DESTINATION ${ZODIAC_13_ROOT_DIR}/bin
# )