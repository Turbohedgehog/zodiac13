cmake_minimum_required(VERSION 3.21)

project(ogre_module)

set(CMAKE_CXX_STANDARD 20)

find_package(SDL2 REQUIRED)
find_package(OGRE REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS dll)

file(
  GLOB_RECURSE
  PROJECT_SOURCE_FILES
  ${PROJECT_SOURCE_DIR}/src/**.cpp
)

add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCE_FILES})
add_library(${PROJECT_NAMESPACE}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}>
)

set(OGRE_MEDIA_LOCATION "Media")
set(ASSETS_LOCATION "assets")
add_definitions(-DOGRE_MEDIA_DIR="${OGRE_MEDIA_LOCATION}")
add_definitions(-DASSETS_DIR="${ASSETS_LOCATION}")


list(REMOVE_ITEM OGRE_LIBRARIES "OgreGLSupportStatic")

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  zodiac13::core
  zodiac13::components
  $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
  ${OGRE_LIBRARIES}
  imgui::imgui
  Codec_Assimp
  Codec_STBI
  Codec_FreeImage
  Plugin_OctreeZone
  Plugin_PCZSceneManager
  Boost::dll
  ${OGRE_RenderSystem_GL3Plus_LIBRARIES}
)

set(MODULE_TARGET_PATH "modules/ogre")

# set(OGRE_TOOLS_PATH "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ogre")

# ToDo: перевести проект на динамическую библиотеку и копировать плагины Ogre в ${PROJECT_NAME} вместо zodiac13
# Сейчас происходит полная завязка на проекте zodiac13, хотя он является дочерним в последовательности
# Идея взята здесь https://github.com/NearEDGE/Ogre-vcpkg-Example-Project/blob/master/Ogre%20vcpkg%20example%20project/CMakeLists.txt#L34
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:zodiac13>/${MODULE_TARGET_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy
        # $<TARGET_FILE:Plugin_DotScene>
        $<TARGET_FILE:RenderSystem_GL3Plus>
        # $<TARGET_FILE:RenderSystem_Direct3D11>
        $<TARGET_FILE:Plugin_ParticleFX>
        $<TARGET_FILE:Plugin_PCZSceneManager>
        $<TARGET_FILE:Plugin_OctreeZone>
        $<TARGET_FILE:Codec_STBI>
        $<TARGET_FILE:Codec_Assimp>
        $<TARGET_FILE:Codec_FreeImage>
        $<TARGET_FILE_DIR:zodiac13>/${MODULE_TARGET_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${OGRE_MEDIA_DIR}
        $<TARGET_FILE_DIR:zodiac13>/${OGRE_MEDIA_LOCATION}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/${ASSETS_LOCATION}
        $<TARGET_FILE_DIR:zodiac13>/${ASSETS_LOCATION}
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/${ASSETS_LOCATION}/"ogre_plugins.cfg"
        $<TARGET_FILE_DIR:zodiac13>/"ogre_plugins.cfg"
    # COMMAND ${CMAKE_COMMAND} -E copy_directory
    #     ${OGRE_TOOLS_PATH}
    #     $<TARGET_FILE_DIR:zodiac13>/tools
)


install(DIRECTORY include/ DESTINATION include/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        RUNTIME_DEPENDENCY_SET runtime_deps
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAMESPACE}_${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAMESPACE}::
        DESTINATION lib/cmake/${PROJECT_NAMESPACE}-${PROJECT_VERSION}/
)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set_target_properties(${PROJECT_NAME} PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/${MODULE_TARGET_PATH}
)
