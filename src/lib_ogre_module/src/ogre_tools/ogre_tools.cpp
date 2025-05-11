#include "ogre_tools.h"

#include <lib_core/log.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <OgreRoot.h>
#include <OgreRenderWindow.h>
#include <OgreConfigFile.h>
#include <OgreCamera.h>
#include <OgreViewport.h>
#include <OgreSceneManager.h>
#include <OgreEntity.h>

// #include <RenderSystems/GL3Plus/OgreGL3PlusPlugin.h>
// #include <OgreGL3PlusPlugin.h>

#include "ogre_datatypes.h"

namespace z13::ogre {

void OgreTools::CreateSDLOgreRoot(OgreData& ogre_data) {
  SDL_Init(SDL_INIT_VIDEO);

  auto* sld_window = SDL_CreateWindow(
      "OGRE + SDL2 Window",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      1280, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
  );

  SDL_SysWMinfo wm_info;
  SDL_VERSION(&wm_info.version);
  if (!SDL_GetWindowWMInfo(sld_window, &wm_info)) {
    LOG_CRITICAL("Could not get window info: {}", SDL_GetError());
    SDL_DestroyWindow(sld_window);
    SDL_Quit();
    return;
  }

  auto ogre_root = std::make_shared<Ogre::Root>("", "", "Ogre.log");

  // ToDo: fix it!!!
#ifdef NDEBUG
  ogre_root->loadPlugin("C:/Projects/vcpkg/buildtrees/ogre/x64-windows-rel/bin/RenderSystem_GL3Plus");
#else
  ogre_root->loadPlugin("C:/Projects/vcpkg/buildtrees/ogre/x64-windows-dbg/bin/RenderSystem_GL3Plus");
#endif

  if (!ogre_root->restoreConfig() && !ogre_root->showConfigDialog(nullptr)) {
    LOG_CRITICAL("Ogre configuration canceled!");
    SDL_DestroyWindow(sld_window);
    SDL_Quit();
    return;
  }

  Ogre::NameValuePairList misc_params;
#if defined(_WIN32)
  misc_params["externalWindowHandle"] = Ogre::StringConverter::toString((size_t)wm_info.info.win.window);
#elif defined(__linux__)
  misc_params["parentWindowHandle"] = Ogre::StringConverter::toString((size_t)wm_info.info.x11.window);
#endif

  auto& render_systems = ogre_root->getAvailableRenderers();
  LOG_WARN("~~~~ render_systems = {}", render_systems.size());
  if (render_systems.empty()) {
    LOG_CRITICAL("NO RENDER SYSTEMS AVALIABLE");
  }
  Ogre::RenderSystem* renderSystem = ogre_root->getAvailableRenderers()[0];
  ogre_root->setRenderSystem(renderSystem);

  ogre_root->initialise(false);
        
  Ogre::RenderWindow* ogre_window = ogre_root->createRenderWindow(
      "OGRE RenderWindow",
      1280, 720,
      false,
      &misc_params
  );
  
  SDL_ShowWindow(sld_window);

  ogre_data.ogre_root = ogre_root;
}

void OgreTools::UpdateSDLOgreWindow(struct OgreData& ogre_data) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      ogre_data.is_window_closed = true;
    }
  }

  if (!ogre_data.ogre_root->renderOneFrame()) {
    ogre_data.is_window_closed = true;
  }
}

void OgreTools::DestroySDLOgreWindow(struct OgreData& ogre_data) {}

}  // namespace z13::ogre
