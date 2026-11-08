#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include "DescriptorSet.h"
#include "TextureWorkersPool.h"
#include "Swapchain.h"
#include "Texture.h"
#include "utils.cpp"
#include "texture/NullTexture.h"

class TextureManager {
public:
  TextureManager(
    vk::Device device,
    vk::Queue graphicsQueue,
    vk::CommandPool commandPool,
    vma::Allocator allocator,
    TextureWorkerPool &workerPool,
    TransferThread &transferThread,
    QueueFamilyIndices &familyIndices,
    DescriptorSet &descriptorSet,
    uint32_t shaderBinding
  );

  ~TextureManager();

  uint32_t loadTextureFromFile(
    const std::filesystem::path &textureParent,
    const std::filesystem::path &filename
  );

  void checkTextureLoading();

  void finalizeUploads();

  void updateDS(DescriptorSet &descriptorSet);

  std::optional<Texture *> getTexture(uint32_t slot);

  void unloadTexture(uint32_t slot);

  std::unordered_map<uint32_t, std::unique_ptr<Texture> > m_textures = {};
private:
  vk::Device m_device = nullptr;
  vk::Queue m_graphicsQueue = nullptr;
  vk::UniqueCommandBuffer m_uploadCmd;
  vk::UniqueFence m_uploadFence;
  DescriptorSet *m_descriptorSet = nullptr;
  TextureWorkerPool *m_workerPool = nullptr;
  TransferThread *m_transferThread = nullptr;
  QueueFamilyIndices *m_familyIndices = nullptr;
  uint32_t m_shaderBinding = 0;

  std::unordered_map<std::string, uint32_t> m_cache = {};
  std::unordered_map<uint32_t, vk::DescriptorImageInfo> m_textureDescriptors = {};
  vk::UniqueSampler m_sampler;
  TextureLoadDone m_pendingLoad = {};
  std::unique_ptr<NullTexture> m_placeholder;

  bool m_uploadPending = false;
  std::vector<uint32_t> m_submittedSlots = {};
  std::unordered_map<uint32_t, std::unique_ptr<Texture> > m_pendingTextures = {};
};


#endif
