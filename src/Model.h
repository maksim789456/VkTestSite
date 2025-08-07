#ifndef MODEL_H
#define MODEL_H

#include <filesystem>
#include <map>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <glm/ext/matrix_float4x4.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include "Mesh.h"
#include "Vertex.h"

struct ModelPushConsts {
  glm::mat4 model;
};

class Model {
  public:
  Model() = default;

  Model(vk::Device device, vma::Allocator allocator, std::filesystem::path modelPath);

  ~Model() = default;
private:
  Mesh<Vertex, uint32_t>* createMesh(vma::Allocator allocator, const aiScene *scene);
  void createCommandBuffers(vk::Device device, vk::CommandPool commandPool, uint32_t imagesCount);
  ModelPushConsts calcPushConsts() const;

  std::string m_name;
  Mesh<Vertex, uint32_t> m_mesh;
  glm::vec3 m_position = {};
  std::map<uint32_t, vk::Image> m_textures; //TODO: texture model
  std::map<uint32_t, vk::DescriptorImageInfo> m_textureDescriptors;
  vk::UniqueSampler m_sampler;
  std::vector<vk::CommandBuffer> m_commandBuffers;
};
#endif //MODEL_H
