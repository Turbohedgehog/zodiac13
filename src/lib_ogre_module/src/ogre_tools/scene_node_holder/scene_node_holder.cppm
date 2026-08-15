// z13.ogre.scene_node_holder module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Перенесён из src/ogre_tools/scene_node_holder/scene_node_holder.h.

module;

#include <Ogre.h>

export module z13.ogre.scene_node_holder;

// Единый механизм импорта стандартной библиотеки (вместо устаревшего
// "export import <memory>;"), чтобы не смешивать хедер-модули с `import std;`.
// Иначе MSVC выдаёт C1116 (_Stop_callback_base::_Do_attach) при импорте
// зависимого модуля z13.ogre.components.
import std.compat;

export namespace z13::ogre {

class SceneNodeHolder;
using SceneNodeHolderPtr = std::shared_ptr<SceneNodeHolder>;
using SceneNodeHolderWeakPtr = std::weak_ptr<SceneNodeHolder>;

class SceneNodeHolder {
 public:
  static SceneNodeHolderWeakPtr CreateSceneNodeHolder(Ogre::SceneNode* scene_node);

  Ogre::SceneNode* operator->();
  const Ogre::SceneNode* operator->() const;
  Ogre::SceneNode* Get();
  const Ogre::SceneNode* Get() const;

 private:
  SceneNodeHolder(Ogre::SceneNode* scene_node);
  static SceneNodeHolderWeakPtr ExtractSceneNodeHolder(const Ogre::SceneNode* scene_node);

  Ogre::SceneNode* scene_node_ = nullptr;
};

}  // namespace z13::ogre