/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "assimp_loader.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <raylib.h>
#include <raymath.h>

#include <stb_image.h>

#include <lib_core/log.h>

#include "asset_path.h"

namespace z13::raylib {

namespace {

namespace fs = std::filesystem;

constexpr unsigned kMaxIndexableVertices = 0xFFFFu;

// stbi_* symbols resolve to raylib's own build (no local STB_IMAGE_IMPLEMENTATION
// here); used directly for JPEG and as a fallback for anything raylib's own
// LoadImageFromMemory rejects.
::Image DecodeImage(const unsigned char* data, int size, const char* hint) {
  const bool is_jpeg =
      hint != nullptr && (TextIsEqual(hint, ".jpg") || TextIsEqual(hint, ".jpeg"));
  if (!is_jpeg) {
    ::Image image = LoadImageFromMemory(hint, data, size);
    if (image.data != nullptr) {
      return image;
    }
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
  if (pixels == nullptr) {
    return ::Image{};
  }

  const auto byte_count = static_cast<unsigned>(width) * static_cast<unsigned>(height) * 4u;
  auto* owned = static_cast<unsigned char*>(MemAlloc(byte_count));
  std::memcpy(owned, pixels, byte_count);
  stbi_image_free(pixels);
  return ::Image{owned, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

struct TaggedMesh {
  const aiMesh* mesh = nullptr;
  aiMatrix4x4 node_transform;
};

// Depth-first node walk accumulating each mesh's world transform (assimp is
// row-major; parent * child is the assimp convention).
void CollectMeshes(const aiScene& scene, const aiNode& node, const aiMatrix4x4& parent,
                   std::vector<TaggedMesh>& out) {
  const aiMatrix4x4 global = parent * node.mTransformation;

  for (unsigned i = 0; i < node.mNumMeshes; ++i) {
    out.push_back(TaggedMesh{scene.mMeshes[node.mMeshes[i]], global});
  }
  for (unsigned i = 0; i < node.mNumChildren; ++i) {
    CollectMeshes(scene, *node.mChildren[i], global, out);
  }
}

::Mesh BuildMesh(const TaggedMesh& tagged) {
  const aiMesh& src = *tagged.mesh;

  if (src.mNumVertices > kMaxIndexableVertices) {
    LOG_WARN("[raylib] assimp: mesh '{}' has {} vertices (> {}); 16-bit indices will wrap",
             src.mName.C_Str(), src.mNumVertices, kMaxIndexableVertices);
    assert(src.mNumVertices <= kMaxIndexableVertices);
  }

  ::Mesh mesh{};
  mesh.vertexCount = static_cast<int>(src.mNumVertices);
  mesh.triangleCount = static_cast<int>(src.mNumFaces);

  const auto vert_bytes = static_cast<unsigned>(src.mNumVertices) * 3u * sizeof(float);
  const auto uv_bytes = static_cast<unsigned>(src.mNumVertices) * 2u * sizeof(float);
  const auto idx_bytes = static_cast<unsigned>(src.mNumFaces) * 3u * sizeof(unsigned short);
  mesh.vertices = static_cast<float*>(MemAlloc(vert_bytes));
  mesh.normals = static_cast<float*>(MemAlloc(vert_bytes));
  mesh.texcoords = static_cast<float*>(MemAlloc(uv_bytes));
  mesh.indices = static_cast<unsigned short*>(MemAlloc(idx_bytes));

  aiMatrix3x3 normal_matrix(tagged.node_transform);
  normal_matrix.Inverse();
  normal_matrix.Transpose();

  for (unsigned v = 0; v < src.mNumVertices; ++v) {
    const aiVector3D position = tagged.node_transform * src.mVertices[v];
    mesh.vertices[v * 3 + 0] = position.x;
    mesh.vertices[v * 3 + 1] = position.y;
    mesh.vertices[v * 3 + 2] = position.z;

    aiVector3D normal = src.mNormals != nullptr ? normal_matrix * src.mNormals[v]
                                                : aiVector3D(0.f, 0.f, 1.f);
    normal.Normalize();
    mesh.normals[v * 3 + 0] = normal.x;
    mesh.normals[v * 3 + 1] = normal.y;
    mesh.normals[v * 3 + 2] = normal.z;

    const bool has_uv = src.mTextureCoords[0] != nullptr;
    mesh.texcoords[v * 2 + 0] = has_uv ? src.mTextureCoords[0][v].x : 0.f;
    mesh.texcoords[v * 2 + 1] = has_uv ? src.mTextureCoords[0][v].y : 0.f;
  }

  for (unsigned f = 0; f < src.mNumFaces; ++f) {
    const aiFace& face = src.mFaces[f];
    assert(face.mNumIndices == 3);  // aiProcess_Triangulate guarantees this
    for (unsigned k = 0; k < 3; ++k) {
      mesh.indices[f * 3 + k] = static_cast<unsigned short>(face.mIndices[k]);
    }
  }

  UploadMesh(&mesh, false);
  return mesh;
}

::Texture2D LoadEmbeddedTexture(const aiTexture& texture) {
  if (texture.mHeight == 0) {  // compressed blob (png/jpg/...)
    const std::string hint =
        texture.achFormatHint[0] != '\0' ? std::string(".") + texture.achFormatHint : ".png";
    ::Image image = DecodeImage(reinterpret_cast<const unsigned char*>(texture.pcData),
                                static_cast<int>(texture.mWidth), hint.c_str());
    ::Texture2D result = LoadTextureFromImage(image);
    UnloadImage(image);
    return result;
  }

  const int pixel_count = static_cast<int>(texture.mWidth) * static_cast<int>(texture.mHeight);
  auto* pixels = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned>(pixel_count) * 4u));
  for (int i = 0; i < pixel_count; ++i) {
    pixels[i * 4 + 0] = texture.pcData[i].r;
    pixels[i * 4 + 1] = texture.pcData[i].g;
    pixels[i * 4 + 2] = texture.pcData[i].b;
    pixels[i * 4 + 3] = texture.pcData[i].a;
  }
  ::Image image{pixels, static_cast<int>(texture.mWidth), static_cast<int>(texture.mHeight), 1,
                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  ::Texture2D result = LoadTextureFromImage(image);
  UnloadImage(image);
  return result;
}

::Texture2D ResolveDiffuseTexture(const aiScene& scene, const aiMaterial& material,
                                  const fs::path& model_dir, const std::string& model_stem) {
  aiString reference;
  if (material.GetTexture(aiTextureType_DIFFUSE, 0, &reference) != AI_SUCCESS &&
      material.GetTexture(aiTextureType_BASE_COLOR, 0, &reference) != AI_SUCCESS) {
    return ::Texture2D{};
  }

  const std::string ref = reference.C_Str();

  // Covers both "*N" indices and FBX media embedded under a filename (the case for
  // spaceship2: the material points at a .fbm path but the JPEG lives in the FBX).
  if (const aiTexture* embedded = scene.GetEmbeddedTexture(ref.c_str())) {
    return LoadEmbeddedTexture(*embedded);
  }

  // fs::path parses both '/' and '\\' as separators, so a "spaceship.fbm\\tex.jpg"
  // reference resolves fine relative to the model directory.
  const fs::path relative = fs::path(ref);
  const fs::path base = relative.filename();
  const fs::path candidates[] = {
      model_dir / relative,
      model_dir / base,
      model_dir / (model_stem + ".fbm") / base,
      relative,
  };
  for (const fs::path& candidate : candidates) {
    const std::string candidate_path = candidate.string();
    if (!FileExists(candidate_path.c_str())) {
      continue;
    }
    int size = 0;
    unsigned char* file_data = LoadFileData(candidate_path.c_str(), &size);
    ::Image image = DecodeImage(file_data, size, GetFileExtension(candidate_path.c_str()));
    UnloadFileData(file_data);
    ::Texture2D result = LoadTextureFromImage(image);
    UnloadImage(image);
    return result;
  }

  LOG_WARN("[raylib] assimp: texture '{}' not found next to the model", ref);
  return ::Texture2D{};
}

}  // namespace

::Model LoadModelFromAsset(std::string_view relative_path) {
  const std::string path = AssetPath(relative_path);

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals |
                aiProcess_FlipUVs);

  if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
      scene->mRootNode == nullptr) {
    LOG_ERROR("[raylib] assimp: cannot load '{}': {}", path, importer.GetErrorString());
    return ::Model{};
  }

  std::vector<TaggedMesh> tagged;
  CollectMeshes(*scene, *scene->mRootNode, aiMatrix4x4(), tagged);
  if (tagged.empty()) {
    LOG_ERROR("[raylib] assimp: '{}' has no meshes", path);
    return ::Model{};
  }

  ::Model model{};
  model.transform = MatrixIdentity();
  model.meshCount = static_cast<int>(tagged.size());
  model.meshes = static_cast<::Mesh*>(MemAlloc(static_cast<unsigned>(model.meshCount) * sizeof(::Mesh)));
  model.meshMaterial =
      static_cast<int*>(MemAlloc(static_cast<unsigned>(model.meshCount) * sizeof(int)));

  model.materialCount = scene->mNumMaterials > 0 ? static_cast<int>(scene->mNumMaterials) : 1;
  model.materials = static_cast<::Material*>(
      MemAlloc(static_cast<unsigned>(model.materialCount) * sizeof(::Material)));

  const fs::path model_dir = fs::path(path).parent_path();
  const std::string model_stem = fs::path(path).stem().string();
  int textured_materials = 0;
  for (int m = 0; m < model.materialCount; ++m) {
    model.materials[m] = LoadMaterialDefault();
    if (m < static_cast<int>(scene->mNumMaterials)) {
      const ::Texture2D texture =
          ResolveDiffuseTexture(*scene, *scene->mMaterials[m], model_dir, model_stem);
      if (texture.id != 0) {
        model.materials[m].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        ++textured_materials;
      }
    }
  }

  unsigned long long total_vertices = 0;
  for (int i = 0; i < model.meshCount; ++i) {
    model.meshes[i] = BuildMesh(tagged[i]);
    total_vertices += static_cast<unsigned>(model.meshes[i].vertexCount);
    const unsigned material_index = tagged[i].mesh->mMaterialIndex;
    model.meshMaterial[i] =
        material_index < static_cast<unsigned>(model.materialCount) ? static_cast<int>(material_index)
                                                                   : 0;
  }

  LOG_INFO("[raylib] assimp: '{}' -> {} mesh(es), {} verts, {} material(s) ({} textured)",
           relative_path, model.meshCount, total_vertices, model.materialCount, textured_materials);
  return model;
}

}  // namespace z13::raylib
