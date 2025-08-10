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
#include "Vertex.h"

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
    std::filesystem::path modelPath
  );

  void createCommandBuffers(vk::Device device, vk::CommandPool commandPool, uint32_t imagesCount);

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

  [[nodiscard]] ModelPushConsts calcPushConsts() const;

  std::string m_name;
  std::unique_ptr<Mesh<Vertex, uint32_t> > m_mesh;
  glm::vec3 m_position = {};
  std::map<uint32_t, vk::Image> m_textures; //TODO: texture model
  std::map<uint32_t, vk::DescriptorImageInfo> m_textureDescriptors;
  vk::UniqueSampler m_sampler;
  std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
};
#endif //MODEL_H
