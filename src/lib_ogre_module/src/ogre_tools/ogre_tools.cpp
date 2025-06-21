#include "ogre_tools.h"

#include <filesystem>

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
#include <OgreOverlaySystem.h>
#include "ogre_import/OgreImGuiOverlay.h"
#include "ogre_import/OgreImGuiInputListener.h"
#include "ogre_import/SDLInputMapping.h"
#include <OgreOverlayManager.h>
#include <OgreShaderPrerequisites.h>
#include <OgreShaderGenerator.h>
#include <OgreSGTechniqueResolverListener.h>

// #include <imgui/backends/imgui_impl_sdl2.h>
// #include <imgui.h>
// #include <imgui/backends/imgui_impl_sdl2.h>
// #include <imgui/backends/imgui_impl_opengl3.h>

#include <RenderSystems/GL3Plus/OgreGL3PlusPlugin.h>

#include "ogre_datatypes.h"

namespace z13::ogre {

Ogre::z13::ImGuiOverlay* InitImguiOverlay() {
  if(auto overlay = Ogre::OverlayManager::getSingleton().getByName("ImGuiOverlay"))
    return static_cast<Ogre::z13::ImGuiOverlay*>(overlay);

  auto imguiOverlay = new Ogre::z13::ImGuiOverlay();
  // LOG_INFO("~~~ InitImguiOverlay = {}; {}", imguiOverlay->getName(), reinterpret_cast<int>(ImGui::GetCurrentContext()));
  Ogre::OverlayManager::getSingleton().addOverlay(imguiOverlay); // now owned by overlaymgr

  // handle DPI scaling
  float vpScale = Ogre::OverlayManager::getSingleton().getPixelRatio();
  // ImGui::CreateContext();
  ImGui::GetStyle().ScaleAllSizes(vpScale);

  // mImGuiListener.reset(new Ogre::ImGuiInputListener());

  return imguiOverlay;
}

void InitImgui() {
  Ogre::z13::ImGuiOverlay* overlay = InitImguiOverlay();
  float vpScale = Ogre::OverlayManager::getSingleton().getPixelRatio();
  ImGui::GetIO().FontGlobalScale = std::round(vpScale); // default font does not work with fractional scaling

  overlay->setZOrder(300);
  overlay->show();
}

void OgreTools::CreateSDLOgreRoot(OgreData& ogre_data) {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  auto* sld_window = SDL_CreateWindow(
      "OGRE + SDL2 Window",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      1280, 720,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
  );

  SDL_GLContext glContext = SDL_GL_CreateContext(sld_window);
  SDL_GL_MakeCurrent(sld_window, glContext);

  SDL_SysWMinfo wm_info;
  SDL_VERSION(&wm_info.version);
  if (!SDL_GetWindowWMInfo(sld_window, &wm_info)) {
    LOG_CRITICAL("Could not get window info: {}", SDL_GetError());
    SDL_DestroyWindow(sld_window);
    SDL_Quit();
    return;
  }

  auto ogre_root = std::make_shared<Ogre::Root>("", "", "Ogre.log");
  ogre_root->installPlugin(OGRE_NEW Ogre::GL3PlusPlugin);

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
  try {
    // auto* rsc = renderSystem->getMutableCapabilities();
    // rsc->setCapability(Ogre::RSC_FIXED_FUNCTION);
  // renderSystem->setConfigOption("Fixed Pipeline Enabled", "Yes");
  } catch (std::exception& ex) {
    LOG_CRITICAL("--- ex = {}", ex.what());
    throw;
  }
  ogre_root->setRenderSystem(renderSystem);

  ogre_root->initialise(false);
        
  Ogre::RenderWindow* ogre_window = ogre_root->createRenderWindow(
      "OGRE RenderWindow",
      1280, 720,
      false,
      &misc_params
  );

  Ogre::SceneManager* scene_manager = ogre_root->createSceneManager("DefaultSceneManager");
  Ogre::Camera* camera = scene_manager->createCamera("MainCamera");
  Ogre::OverlaySystem* overlay_system = OGRE_NEW Ogre::OverlaySystem();
  scene_manager->addRenderQueueListener(overlay_system);

  if (Ogre::RTShader::ShaderGenerator::initialize()) {
    auto* shader_generator = Ogre::RTShader::ShaderGenerator::getSingletonPtr();

        // Create and register the material manager listener if it doesn't exist yet.
    auto* material_mgr_listener = new OgreBites::SGTechniqueResolverListener(shader_generator);
    Ogre::MaterialManager::getSingleton().addListener(material_mgr_listener);
    shader_generator->addSceneManager(scene_manager);
  }

  auto& rgm = Ogre::ResourceGroupManager::getSingleton();
  auto media_path = (std::filesystem::path(OGRE_MEDIA_DIR));
  LOG_INFO("!!!!! media_path = {}", media_path.string());
  rgm.addResourceLocation((media_path / "Main").string(), "FileSystem", Ogre::RGN_INTERNAL);
  rgm.addResourceLocation((media_path / "RTShaderLib").string(), "FileSystem", Ogre::RGN_INTERNAL);

  Ogre::SceneNode* camera_node = scene_manager->getRootSceneNode()->createChildSceneNode();
  camera_node->attachObject(camera);
  camera_node->setPosition(0,0,500);
  camera_node->lookAt(Ogre::Vector3(0, 0, 0), Ogre::Node::TS_PARENT);
  Ogre::Viewport* viewport = ogre_window->addViewport(camera);
  viewport->setBackgroundColour(Ogre::ColourValue(0.2f, 0.3f, 0.4f));
  InitImgui();
  
  SDL_ShowWindow(sld_window);

  ogre_data.ogre_root = ogre_root;
  ogre_data.input_listener = std::make_shared<OgreBites::z13::ImGuiInputListener>();
}

void OgreTools::UpdateSDLOgreWindow(OgreData& ogre_data) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      ogre_data.is_window_closed = true;
    }

    auto ogre_event = Ogre::z13::convert(event);
    Ogre::z13::ProcessEventToListener(ogre_event, ogre_data.input_listener.get());
  }

  Ogre::z13::ImGuiOverlay::NewFrame();
  ImGui::ShowDemoWindow();

  try {
    if (!ogre_data.ogre_root->renderOneFrame()) {
      ogre_data.is_window_closed = true;
    }
  }
  catch(std::exception& ex) {
    LOG_CRITICAL("--- ex = {}", ex.what());
    throw;
  }
}

void OgreTools::DestroySDLOgreWindow(OgreData& ogre_data) {}

}  // namespace z13::ogre
