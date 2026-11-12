#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include "../DescriptorSet.h"
#include "TextureWorkersPool.h"
#include "../Swapchain.h"
#include "Texture.h"
#include "../utils.cpp"
#include "NullTexture.h"

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

  std::optional<Texture *> getTexture(uint32_t slot);

  void unloadTexture(uint32_t slot);

  void drawUI();

private:
  /// Loaded on the worker threads, upload + mip pass not finished yet.
  struct PendingTexture {
    std::unique_ptr<Texture> texture;
    uint32_t uploadId = 0;
  };

  /// Slot + upload id of a graphics-side pass submitted with m_uploadCmd.
  struct SubmittedUpload {
    uint32_t slot = 0;
    uint32_t uploadId = 0;
  };

  vk::Device m_device = nullptr;
  vk::Queue m_graphicsQueue = nullptr;
  vk::UniqueCommandBuffer m_uploadCmd;
  vk::UniqueFence m_uploadFence;
  DescriptorSet *m_descriptorSet = nullptr;
  TextureWorkerPool *m_workerPool = nullptr;
  TransferThread *m_transferThread = nullptr;
  QueueFamilyIndices *m_familyIndices = nullptr;
  uint32_t m_shaderBinding = 0;

  std::unordered_map<uint32_t, std::unique_ptr<Texture> > m_textures = {};
  std::unordered_map<std::string, uint32_t> m_cache = {};
  std::unordered_map<uint32_t, vk::DescriptorImageInfo> m_textureDescriptors = {};
  vk::UniqueSampler m_sampler;

  TextureLoadDone m_pendingDoneJob = {};
  std::unique_ptr<NullTexture> m_placeholder;
  uint32_t m_nextUploadId = 1;

  bool m_uploadPending = false;
  std::vector<SubmittedUpload> m_submittedSlots = {};
  std::unordered_map<uint32_t, PendingTexture> m_pendingTextures = {};

  /// Textures unloaded while their transfer may still be running
  std::unordered_map<uint32_t, std::unique_ptr<Texture> > m_retiredTextures = {};
};


#endif
