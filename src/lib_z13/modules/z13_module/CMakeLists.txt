cmake_minimum_required(VERSION 3.21)

project(z13_module)

set(CMAKE_CXX_STANDARD 20)

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(platform_folders REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS bimap)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

file(
  GLOB_RECURSE
  PROTO_FILES
  ${PROJECT_SOURCE_DIR}/proto/*.proto
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# if(MSVC)
#   set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_OPTIONS "/W0")
#   set_source_files_properties(${PROTO_HDRS} PROPERTIES COMPILE_OPTIONS "/W0")
# elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
#   set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
#   set_source_files_properties(${PROTO_HDRS} PROPERTIES COMPILE_OPTIONS "-w")
# endif()

add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCE_FILES} ${PROTO_SRCS} ${PROTO_HDRS})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  # $<BUILD_INTERFACE:${Protobuf_INCLUDE_DIRS}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

set_target_properties(
  ${PROJECT_NAME} PROPERTIES
  PROTO_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  Eigen3::Eigen
  sago::platform_folders
  protobuf::libprotobuf
  # protobuf::libprotobuf-lite
  # protobuf::libprotoc
  Boost::bimap
)

# disables message_lite.h(1191,1): warning C4141
target_compile_options(${PROJECT_NAME} PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/wd4141>
)

# set_target_properties(${PROJECT_NAME} PROPERTIES 
#     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
# )

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)

