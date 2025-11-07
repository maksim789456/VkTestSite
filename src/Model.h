#ifndef MODEL_H
#define MODEL_H

#define GLM_ENABLE_EXPERIMENTAL
#include <filesystem>
#include <map>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtx/transform.hpp>

#include "DescriptorSet.h"
#include "Mesh.h"
#include "Swapchain.h"
#include "Texture.h"
#include "TextureManager.h"
#include "Vertex.h"
#include "Transform.h"
#include "utils.cpp"
#include <tracy/TracyVulkan.hpp>

struct alignas(16) ModelPushConsts {
  glm::mat4 model;
};

struct Submesh {
  std::unique_ptr<Mesh<Vertex, uint32_t> > mesh;
  bool enabled = true;
  uint32_t materialIndex;
  glm::mat4 transform = glm::mat4(1.0f);
  std::string name;
};

struct Material {
  uint32_t albedoTexIdx = 99;
  uint32_t normalTexIdx = 99;
  glm::vec4 diffuseColor = glm::vec4(1.0f);
};



class Model {
public:
  Model() = default;

  Model(
    vk::Device device,
    vk::Queue graphicsQueue,
    vk::CommandPool commandPool,
    vma::Allocator allocator,
    TextureManager &textureManager,
    const std::filesystem::path &modelPath
  );

  void createCommandBuffers(vk::Device device, vk::CommandPool commandPool, uint32_t imagesCount);

  vk::CommandBuffer cmdDraw(
    tracy::VkCtx &tracyCtx,
    vk::Framebuffer framebuffer,
    vk::RenderPass renderPass,
    vk::Pipeline pipeline,
    const Swapchain &swapchain,
    const DescriptorSet &descriptorSet,
    uint32_t subpass,
    uint32_t imageIndex
  );

  void drawUI();

  ~Model() = default;

private:
  void processNode(
    const aiNode *node,
    const aiScene *scene,
    const glm::mat4 &parentTransform
  );

  void processMaterials(
    TextureManager &textureManager,
    const aiScene *scene,
    const std::filesystem::path &modelParent
  );

  std::unique_ptr<Mesh<Vertex, uint32_t>> createMesh(
    const aiMesh *mesh,
    const aiScene *scene,
    const glm::mat4 &transform
  );

  [[nodiscard]] ModelPushConsts calcPushConsts(const glm::mat4 &transform) const;

  std::string m_name;
  Transform m_transform;
  std::vector<Submesh> m_submeshes;
  std::vector<Material> m_materials;
  std::vector<vk::UniqueCommandBuffer> m_commandBuffers;

  vk::Device m_device = nullptr;
  vk::Queue m_graphicsQueue = nullptr;
  vk::CommandPool m_commandPool = nullptr;
  vma::Allocator m_allocator = nullptr;
};
#endif //MODEL_H
