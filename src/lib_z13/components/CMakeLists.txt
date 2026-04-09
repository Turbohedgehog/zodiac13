cmake_minimum_required(VERSION 3.21)

project(components)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS container)

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})


target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    ${PROJECT_NAME}
    INTERFACE
    zodiac13::fbs_schemas
    Boost::container
)

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