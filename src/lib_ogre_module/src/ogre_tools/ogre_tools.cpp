#include "ogre_tools.h"

#include <filesystem>

#include <lib_core/log.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <ogre_module/ogre_datatypes.h>
#include <ogre_module/ogre_components.h>
#include <z13_module/components/gameplay.h>
#include <z13_module/components/geometry.h>

#include <Ogre.h>

#include <OgreRoot.h>
#include <OgreOverlaySystem.h>
#include "ogre_import/OgreImGuiOverlay.h"
#include "ogre_import/OgreImGuiInputListener.h"
#include "ogre_import/SDLInputMapping.h"
#include <OgreOverlayManager.h>
#include <OgreShaderPrerequisites.h>
#include <OgreShaderGenerator.h>
#include <OgreSGTechniqueResolverListener.h>

#include <RenderSystems/GL3Plus/OgreGL3PlusPlugin.h>
#include <Plugins/Assimp/OgreAssimpLoader.h>
// #include <Plugins/FreeImageCodec/OgreFreeImageCodec.h>
#include <Plugins/STBICodec/OgreSTBICodec.h>
#include "input_publisher/input_publisher.h"

namespace z13::ogre {

Ogre::z13::ImGuiOverlay* InitImguiOverlay() {
  if(auto overlay = Ogre::OverlayManager::getSingleton().getByName("ImGuiOverlay"))
    return static_cast<Ogre::z13::ImGuiOverlay*>(overlay);

  auto imguiOverlay = new Ogre::z13::ImGuiOverlay();
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

void CreateSkyboxMaterial() {
  Ogre::MaterialManager& matMgr = Ogre::MaterialManager::getSingleton();

  // Создаем материал для скайбокса
  Ogre::MaterialPtr skyboxMaterial = matMgr.create("MySkybox",
      kAssetsResourceGroup);
      // Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

  // Получаем технику и проход
  Ogre::Technique* tech = skyboxMaterial->createTechnique();
  Ogre::Pass* pass = tech->createPass();

  // Настраиваем проход для скайбокса
  pass->setDepthCheckEnabled(false);      // Отключаем проверку глубины
  pass->setDepthWriteEnabled(false);      // Отключаем запись глубины
  pass->setLightingEnabled(false);        // Отключаем освещение
  pass->setCullingMode(Ogre::CULL_NONE);  // Отключаем отсечение граней

  // Создаем текстурный юнит
  Ogre::TextureUnitState* texUnit = pass->createTextureUnitState();

  std::array<std::string, 6> cm = {"textures/skybox/skybox1.jpg", "textures/skybox/skybox1.jpg", 
                              "textures/skybox/skybox1.jpg", "textures/skybox/skybox1.jpg",
                              "textures/skybox/skybox1.jpg", "textures/skybox/skybox1.jpg"};

  // Вариант 1: Кубическая текстура из 6 отдельных файлов
  // texUnit->setCubicTextureName(cm.data(), true);

  // ИЛИ Вариант 2: Готовый DDS файл с кубической картой
  texUnit->setTextureName("textures/skybox/vz_techno_cubemap_ue.dds", Ogre::TEX_TYPE_CUBE_MAP);

  // Настраиваем фильтрацию
  texUnit->setTextureFiltering(Ogre::TFO_ANISOTROPIC);
  texUnit->setTextureAnisotropy(8);

  // Устанавливаем адресацию текстуры - зажимаем к краям
  texUnit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);

  // Включаем скайбокс в сцене
  // mSceneMgr->setSkyBox(true, "MySkyBoxMaterial");
}

void CreateSkyboxMaterial2() {
  Ogre::MaterialManager& matMgr = Ogre::MaterialManager::getSingleton();
  Ogre::MaterialPtr skyboxMaterial = matMgr.create("MySkybox", kAssetsResourceGroup);

  Ogre::Technique* tech = skyboxMaterial->createTechnique();
  Ogre::Pass* pass = tech->createPass();
  
  skyboxMaterial->load();
  bool valid = skyboxMaterial->getBestTechnique() && skyboxMaterial->getBestTechnique()->getNumPasses();
  LOG_INFO("~~~~ CreateSkyboxMaterial2: valid = {}", valid);

  auto* p = skyboxMaterial->getBestTechnique()->getPass(0);
  LOG_INFO("~~~~ getNumTextureUnitStates: p->getNumTextureUnitStates() = {}", p->getNumTextureUnitStates());
        valid = p->getNumTextureUnitStates() &&
                p->getTextureUnitState(0)->getTextureType() == Ogre::TEX_TYPE_CUBE_MAP;
  LOG_INFO("~~~~ CreateSkyboxMaterial2: valid2 = {}", valid);

  // Настраиваем проход для скайбокса
  pass->setDepthCheckEnabled(false);
  pass->setDepthWriteEnabled(false);
  pass->setLightingEnabled(false);
  pass->setCullingMode(Ogre::CULL_NONE);

  // Создаем текстурный юнит
  Ogre::TextureUnitState* texUnit = pass->createTextureUnitState();

  // Указываем единую текстуру
  texUnit->setTextureName("textures/skybox/vz_techno_cubemap_ue.dds", Ogre::TEX_TYPE_CUBE_MAP);
  // texUnit->setTextureName("textures/skybox/skybox1.jpg", Ogre::TEX_TYPE_2D); // Ваша текстура со всеми сторонами

  // Настраиваем фильтрацию
  texUnit->setTextureFiltering(Ogre::TFO_TRILINEAR);
  texUnit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);

  skyboxMaterial->load();
  // skyboxMaterial->setLightingEnabled(false);
  skyboxMaterial->setDepthCheckEnabled(false);
  skyboxMaterial->setReceiveShadows(false);
}

void OgreTools::CreateSdlOgreRoot(flecs::world world, OgreData& ogre_data) {
  // SDL_SCANCODE_SPACE
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  auto* sld_window = SDL_CreateWindow(
      "Zodiac 13",
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

  auto plugin_file_path = std::filesystem::path(ASSETS_DIR) / "plugins.cfg";

  // auto ogre_root = std::make_shared<Ogre::Root>(plugin_file_path.string(), "", "Ogre.log");
  // auto ogre_root = std::make_shared<Ogre::Root>("plugins.cfg", "", "Ogre.log");
  auto ogre_root = std::make_shared<Ogre::Root>("plugins.cfg", "", "Ogre.log");
  ogre_root->installPlugin(OGRE_NEW Ogre::GL3PlusPlugin);
  ogre_root->installPlugin(OGRE_NEW Ogre::AssimpPlugin);
  // ogre_root->installPlugin(OGRE_NEW Ogre::Codec_STBI);
  // ogre_root->installPlugin(OGRE_NEW Ogre::FreeImageCodec);

  auto& rgm = Ogre::ResourceGroupManager::getSingleton();
  auto media_path = std::filesystem::path(OGRE_MEDIA_DIR);
  rgm.addResourceLocation((media_path / "Main").string(), "FileSystem", Ogre::RGN_INTERNAL);
  rgm.addResourceLocation((media_path / "RTShaderLib").string(), "FileSystem", Ogre::RGN_INTERNAL);

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

  auto* scene_manager = ogre_root->createSceneManager("DefaultSceneManager");
  auto* mesh_manager = ogre_root->getMeshManager();
  if (Ogre::RTShader::ShaderGenerator::initialize()) {
    auto* shader_generator = Ogre::RTShader::ShaderGenerator::getSingletonPtr();

    // Create and register the material manager listener if it doesn't exist yet.
    auto* material_mgr_listener = new OgreBites::SGTechniqueResolverListener(shader_generator);
    Ogre::MaterialManager::getSingleton().addListener(material_mgr_listener);
    shader_generator->addSceneManager(scene_manager);
  }

  auto* overlay_system = OGRE_NEW Ogre::OverlaySystem();
  scene_manager->addRenderQueueListener(overlay_system);

#if 0
  auto* camera = scene_manager->createCamera("MainCamera");
  camera->setNearClipDistance(0.1);
  camera->setFarClipDistance(10000);
  // auto* overlay_system = OGRE_NEW Ogre::OverlaySystem();
  // scene_manager->addRenderQueueListener(overlay_system);

  auto* camera_node = scene_manager->getRootSceneNode()->createChildSceneNode();
  camera_node->attachObject(camera);
  camera_node->setPosition(0, 0, 0);
  camera_node->lookAt(Ogre::Vector3(0, 0, 500), Ogre::Node::TS_PARENT);
  auto* viewport = ogre_window->addViewport(camera);
  viewport->setBackgroundColour(Ogre::ColourValue(0.4f, 0.5f, 0.7f));
#endif

  auto assets_path = std::filesystem::path(ASSETS_DIR);
  rgm.addResourceLocation((assets_path).string(), "FileSystem", kAssetsResourceGroup, true);
  rgm.initialiseAllResourceGroups();
  Ogre::MaterialManager& matMgr = Ogre::MaterialManager::getSingleton();
  auto mat = matMgr.load("materials/skybox/skybox1.material", kAssetsResourceGroup);
  auto sky_mat = matMgr.getByName("skybox/skybox");
  if (sky_mat->getNumTechniques() > 0 ) {
    if (sky_mat->getTechnique(0)->getNumPasses() > 0) {
    }
  }

  scene_manager->setSkyBox(true, "skybox/skybox"); // ,5000, true, Ogre::Quaternion::IDENTITY, kAssetsResourceGroup);

  InitImgui();
  
  SDL_ShowWindow(sld_window);

  ogre_data.ogre_root = ogre_root;
  ogre_data.ogre_window = ogre_window;
  ogre_data.input_listener = std::make_shared<OgreBites::z13::ImGuiInputListener>();

  if (!world.has<gameplay::Pause>()) {
    OgreTools::EnableRelativeMouseMode(nullptr);
  }

#if 0
  auto* camera = scene_manager->createCamera("MainCamera");
  camera->setNearClipDistance(0.1);
  camera->setFarClipDistance(10000);
  // auto* overlay_system = OGRE_NEW Ogre::OverlaySystem();
  // scene_manager->addRenderQueueListener(overlay_system);

  auto* camera_node = scene_manager->getRootSceneNode()->createChildSceneNode();
  camera_node->attachObject(camera);
  camera_node->setPosition(0, 0, 0);
  camera_node->lookAt(Ogre::Vector3(0, 0, 500), Ogre::Node::TS_PARENT);
  auto* viewport = ogre_window->addViewport(camera);
  viewport->setBackgroundColour(Ogre::ColourValue(0.4f, 0.5f, 0.7f));
#endif
}

void OgreTools::CreateCamera(const gameplay::Camera& camera, OgreData& ogre_data) {
  // LOG_INFO("~~~~ CreateCamera");
  auto* scene_manager = ogre_data.ogre_root->getSceneManagers().begin()->second;
  auto* ogre_camera = scene_manager->createCamera(camera.name);
  ogre_camera->setNearClipDistance(0.1);
  ogre_camera->setFarClipDistance(10000);

  auto* camera_node = scene_manager->getRootSceneNode()->createChildSceneNode();
  camera_node->attachObject(ogre_camera);
  camera_node->setPosition(0, 0, 0);
  camera_node->lookAt(Ogre::Vector3(0, 0, 500), Ogre::Node::TS_PARENT);
  auto* viewport = ogre_data.ogre_window->addViewport(ogre_camera);
  viewport->setBackgroundColour(Ogre::ColourValue(0.4f, 0.5f, 0.7f));
}

void OgreTools::ReadSdlEvents(flecs::world world, OgreData& ogre_data) {
  SDL_Event event;

  // sdl_input.keyboadr_scancodes = SDL_GetKeyboardState(nullptr);

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        world.add<OgreWindowClosed>();
        ogre_data.ogre_root->queueEndRendering();
        break;

      case SDL_KEYDOWN:
        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
          if (world.has<gameplay::Pause>()) {
            world.remove_all<gameplay::Pause>();
          } else {
            world.add<gameplay::Pause>();
          }
        }
        break;

      case SDL_WINDOWEVENT:
        switch (event.window.event) {
          case SDL_WINDOWEVENT_RESIZED:
            ogre_data.ogre_window->resize(event.window.data1, event.window.data2);
            break;

          case SDL_WINDOWEVENT_FOCUS_GAINED:
            break;

          case SDL_WINDOWEVENT_FOCUS_LOST:
            world.add<gameplay::Pause>();
            break;
        }

        break;
    }

    auto ogre_event = Ogre::z13::convert(event);
    InputPublisher::PublishInput(world, ogre_event);
    Ogre::z13::ProcessEventToListener(ogre_event, ogre_data.input_listener.get());
  }
}

void OgreTools::EnableRelativeMouseMode(const gameplay::Pause*) {
  SDL_SetRelativeMouseMode(SDL_TRUE);
}

void OgreTools::DisableRelativeMouseMode(const gameplay::Pause*) {
  SDL_SetRelativeMouseMode(SDL_FALSE);
}

void OgreTools::RenderSdlOgreWindow(flecs::world world, OgreData& ogre_data) {
  try {
    if (!ogre_data.ogre_root->renderOneFrame()) {
      world.add<OgreWindowClosed>();
    }
  } catch(std::exception& ex) {
    LOG_CRITICAL("--- ex = {}", ex.what());
    throw;
  }
}

void OgreTools::DestroySdlOgreWindow(OgreData& ogre_data) {}

void OgreTools::UpdateCamera(const gameplay::Camera& camera, const geometry::Transform& transform, OgreData& ogre_data) {
  auto* scene_manager = ogre_data.ogre_root->getSceneManagers().begin()->second;
  auto* ogre_camera = scene_manager->getCamera(camera.name);
  auto* camera_scene_node = ogre_camera->getParentSceneNode();
  const auto& position = transform.position;
  const auto& rotation = transform.rotation;
  camera_scene_node->setPosition(position.x, position.y, position.z);
  camera_scene_node->setOrientation(rotation.w, rotation.x, rotation.y, rotation.z);
}

}  // namespace z13::ogre
