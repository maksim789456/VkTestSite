#ifndef MODEL_H
#define MODEL_H

#include <filesystem>
#include <map>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/ext/matrix_float4x4.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include "DescriptorSet.h"
#include "Mesh.h"
#include "Swapchain.h"
#include "Texture.h"
#include "Vertex.h"
#include "utils.cpp"

struct alignas(16) ModelPushConsts {
  glm::mat4 model;
};

class Model {
public:
  Model() = default;

  Model(
    vk::Device device,
    vk::Queue graphicsQueue,
    vk::CommandPool commandPool,
    vma::Allocator allocator,
    const std::filesystem::path &modelPath
  );

  void createCommandBuffers(vk::Device device, vk::CommandPool commandPool, uint32_t imagesCount);

  void updateTextureDescriptors(
    vk::Device device,
    const DescriptorSet &descriptorSet,
    uint32_t imageCount,
    uint32_t shaderBinding
  );

  vk::CommandBuffer cmdDraw(
    vk::Framebuffer framebuffer,
    vk::RenderPass renderPass,
    vk::Pipeline pipeline,
    const Swapchain &swapchain,
    const DescriptorSet &descriptorSet,
    uint32_t subpass,
    uint32_t imageIndex
  );

  ~Model() = default;

private:
  void createMesh(
    vk::Device device,
    vk::Queue graphicsQueue,
    vk::CommandPool commandPool,
    vma::Allocator allocator,
    const aiScene *scene
  );

  void processNode(
    const aiNode *node,
    const aiScene *scene,
    const glm::mat4 &parentTransform,
    std::vector<Vertex> &vertices,
    std::vector<uint32_t> &indices
  );

  void loadTextures(
    vk::Device device,
    vk::Queue graphicsQueue,
    vk::CommandPool commandPool,
    vma::Allocator allocator,
    const aiScene *scene,
    const std::filesystem::path &modelParent
  );

  void createTextureInfos() {
    for (auto &[texIdx, texture]: m_textures) {
      m_textureDescriptors[texIdx] = vk::DescriptorImageInfo(
        m_sampler.get(), texture->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
    }
  }

  [[nodiscard]] ModelPushConsts calcPushConsts() const;

  std::string m_name;
  std::unique_ptr<Mesh<Vertex, uint32_t> > m_mesh;
  glm::vec3 m_position = {};
  std::map<uint32_t, std::unique_ptr<Texture> > m_textures = {};
  std::map<uint32_t, vk::DescriptorImageInfo> m_textureDescriptors = {};
  vk::UniqueSampler m_sampler;
  std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
};
#endif //MODEL_H
