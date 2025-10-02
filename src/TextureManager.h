#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include "DescriptorSet.h"
#include "TextureWorkersPool.h"
#include "Swapchain.h"
#include "Texture.h"
#include "utils.cpp"

class TextureManager {
public:
  TextureManager(
    vk::Device device,
    vk::Queue graphicsQueue,
    vk::CommandPool commandPool,
    TextureWorkerPool &workerPool,
    DescriptorSet &descriptorSet,
    uint32_t shaderBinding
  );

  ~TextureManager() = default;

  uint32_t loadTextureFromFile(
    const std::filesystem::path &textureParent,
    const std::filesystem::path &filename,
    vk::Format format = vk::Format::eR8G8B8A8Unorm
  );

  void checkTextureLoading();

  void updateDS(DescriptorSet& descriptorSet);

  std::optional<Texture *> getTexture(uint32_t slot);

  void unloadTexture(uint32_t slot);

  std::unordered_map<uint32_t, std::unique_ptr<Texture> > m_textures = {};
private:
  vk::Device m_device = nullptr;
  vk::Queue m_graphicsQueue = nullptr;
  vk::CommandPool m_commandPool = nullptr;
  DescriptorSet *m_descriptorSet = nullptr;
  TextureWorkerPool *m_workerPool = nullptr;
  uint32_t m_shaderBinding = 0;

  std::unordered_map<std::string, uint32_t> m_cache = {};
  std::unordered_map<uint32_t, vk::DescriptorImageInfo> m_textureDescriptors = {};
  vk::UniqueSampler m_sampler;
};


#endif
