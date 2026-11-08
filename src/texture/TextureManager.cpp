#include "TextureManager.h"

TextureManager::TextureManager(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  const vma::Allocator allocator,
  TextureWorkerPool &workerPool,
  TransferThread &transferThread,
  QueueFamilyIndices &familyIndices,
  DescriptorSet &descriptorSet,
  const uint32_t shaderBinding
) : m_device(device), m_graphicsQueue(graphicsQueue), m_descriptorSet(&descriptorSet), m_workerPool(&workerPool),
    m_transferThread(&transferThread), m_familyIndices(&familyIndices), m_shaderBinding(shaderBinding) {
  ZoneScoped;
  m_sampler = createSamplerUnique(device);

  const auto transferCmdInfo = vk::CommandBufferAllocateInfo(
    commandPool, vk::CommandBufferLevel::ePrimary, 1);
  m_uploadCmd = std::move(m_device.allocateCommandBuffersUnique(transferCmdInfo).front());
  m_uploadFence = m_device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});

  // Makes placeholder and fill all DS slots
  // TODO: Gate the draw?
  m_placeholder = std::make_unique<NullTexture>(device, allocator, graphicsQueue, commandPool);
  for (uint32_t slot = 0; slot < MAX_TEXTURE_PER_DESCRIPTOR; ++slot) {
    m_textureDescriptors[slot] = vk::DescriptorImageInfo(
      m_sampler.get(), m_placeholder->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
    m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot, m_textureDescriptors[slot]);
  }
}

TextureManager::~TextureManager() {
  ZoneScoped;
  m_device.waitIdle();
  m_pendingTextures.clear();
  m_retiredTextures.clear();
  m_submittedSlots.clear();
}

uint32_t TextureManager::loadTextureFromFile(
  const std::filesystem::path &textureParent,
  const std::filesystem::path &filename
) {
  ZoneScoped;
  const auto texturePath = textureParent / filename;

  if (const auto it = m_cache.find(filename.string()); it != m_cache.end()) {
    spdlog::info(std::format("Reuse texture {} from {}", filename.string(), it->second));
    return it->second;
  }

  uint32_t slot = 0;
  for (; slot < MAX_TEXTURE_PER_DESCRIPTOR; ++slot) {
    if (!m_textures.contains(slot)) {
      break;
    }
  }

  if (slot >= MAX_TEXTURE_PER_DESCRIPTOR) {
    spdlog::error("Texture store full (limit = {})", MAX_TEXTURE_PER_DESCRIPTOR);
    return EMPTY_TEXTURE_INDEX;
  }

  spdlog::info(std::format("Push texture loading job: file {} at slot {}", filename.string(), slot));
  const auto textureJob = TextureLoadJob{
    .texIndex = slot,
    .uploadId = m_nextUploadId++,
    .filepath = texturePath,
  };

  m_workerPool->pushJob(textureJob);
  m_textures[slot] = nullptr;
  m_cache[filename.string()] = slot;

  return slot;
}

void TextureManager::checkTextureLoading() {
  while (m_workerPool->tryDequeueDone(m_pendingDoneJob)) {
    ZoneScopedN("Loaded texture move");
    const auto slot = m_pendingDoneJob.job.texIndex;
    if (!m_pendingDoneJob.texture) {
      spdlog::error("Texture worker return empty texture for slot {}", slot);
      continue;
    }
    if (slot >= MAX_TEXTURE_PER_DESCRIPTOR) {
      spdlog::error("Texture {} has invalid slot {}", m_pendingDoneJob.job.filepath.string(), slot);
      continue;
    }
    if (const auto it = m_textures.find(slot); it != m_textures.end() && it->second != nullptr) {
      spdlog::error("Texture {} overwrite occupied slot {}", m_pendingDoneJob.job.filepath.string(), slot);
    }

    m_pendingTextures[slot] = PendingTexture{
      .texture = std::move(m_pendingDoneJob.texture),
      .uploadId = m_pendingDoneJob.job.uploadId,
    };
  }
}

void TextureManager::finalizeUploads() {
  ZoneScoped;
  if (m_uploadPending) {
    if (m_device.getFenceStatus(*m_uploadFence) != vk::Result::eSuccess)
      return;

    for (const auto &[slot, uploadId]: m_submittedSlots) {
      ZoneScopedN("Bind finished texture");
      const auto pending = m_pendingTextures.find(slot);
      // Slot was unloaded or reused by a newer upload while this pass was running.
      if (pending == m_pendingTextures.end() || pending->second.uploadId != uploadId)
        continue;

      const auto &texture = m_textures[slot] = std::move(pending->second.texture);
      m_pendingTextures.erase(pending);

      texture->createImguiView();
      m_textureDescriptors[slot] = vk::DescriptorImageInfo(
        m_sampler.get(), texture->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
      m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot, m_textureDescriptors[slot]);

      spdlog::info("Texture slot {} uploaded and ready", slot);
    }

    m_submittedSlots.clear();
    m_uploadPending = false;
  }

  TextureUploadFinished tmpFinished = {};
  std::vector<TextureUploadFinished> finishedUploads;
  while (m_transferThread->tryDequeueFinished(tmpFinished)) {
    finishedUploads.push_back(tmpFinished);
  }
  if (finishedUploads.empty())
    return;

  m_uploadCmd->reset();
  m_uploadCmd->begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

  vk::Semaphore waitSemaphore = nullptr;
  uint64_t waitValue = 0;
  m_submittedSlots.clear();

  for (auto &uploadJob: finishedUploads) {
    ZoneScopedN("Record upload finish");

    m_retiredTextures.erase(uploadJob.uploadId);

    const auto pending = m_pendingTextures.find(uploadJob.slot);
    if (pending == m_pendingTextures.end() || pending->second.uploadId != uploadJob.uploadId)
      continue;

    if (!waitSemaphore)
      waitSemaphore = uploadJob.waitSemaphore;
    waitValue = std::max(waitValue, uploadJob.waitValue);

    if (m_familyIndices->isTransferQueueSeparated()) {
      cmdTransferImageOwnership(
        *m_uploadCmd,
        uploadJob.dstImage,
        uploadJob.subresourceRange,
        Acquire,
        *m_familyIndices->transfer,
        *m_familyIndices->graphics
      );
    }

    if (uploadJob.doGenerateMipmaps) {
      cmdGenerateMipmaps(
        *m_uploadCmd,
        uploadJob.dstImage,
        uploadJob.baseExtent.width, uploadJob.baseExtent.height,
        uploadJob.subresourceRange.levelCount,
        uploadJob.dstImageLayout
      );
    } else {
      cmdTransitionImageLayout2(
        *m_uploadCmd,
        uploadJob.dstImage,
        vk::ImageLayout::eTransferDstOptimal,
        uploadJob.dstImageLayout,
        uploadJob.subresourceRange
      );
    }

    m_submittedSlots.push_back({uploadJob.slot, uploadJob.uploadId});
  }

  m_uploadCmd->end();
  if (m_submittedSlots.empty())
    return;

  m_device.resetFences(*m_uploadFence);

  const auto waitInfo = vk::SemaphoreSubmitInfo(waitSemaphore, waitValue, vk::PipelineStageFlagBits2::eAllCommands);
  const auto cmdInfo = vk::CommandBufferSubmitInfo(*m_uploadCmd);
  m_graphicsQueue.submit2(vk::SubmitInfo2({}, waitInfo, cmdInfo, {}), *m_uploadFence);

  m_uploadPending = true;
}

std::optional<Texture *> TextureManager::getTexture(const uint32_t slot) {
  if (const auto tex = m_textures.find(slot); tex != m_textures.end()) {
    if (tex->second == nullptr) return std::nullopt;
    return tex->second.get();
  }

  return std::nullopt;
}

void TextureManager::unloadTexture(const uint32_t slot) {
  ZoneScoped;
  spdlog::debug("Unloading texture {}", slot);

  const auto isSlotUploading = std::ranges::any_of(
    m_submittedSlots, [slot](const SubmittedUpload &upload) { return upload.slot == slot; });

  // The in-flight graphics pass may still reference this image.
  if (m_uploadPending && isSlotUploading)
    m_device.waitIdle();

  if (const auto pending = m_pendingTextures.find(slot); pending != m_pendingTextures.end()) {
    if (isSlotUploading) {
      // Transfer and mip pass both completed (waitIdle above), nothing references it.
      m_pendingTextures.erase(pending);
    } else {
      // The transfer thread may still be copying into this image: keep it alive until
      // its TextureUploadFinished arrives, otherwise the GPU writes into freed memory.
      m_retiredTextures[pending->second.uploadId] = std::move(pending->second.texture);
      m_pendingTextures.erase(pending);
    }
  }

  std::erase_if(m_submittedSlots, [slot](const SubmittedUpload &upload) { return upload.slot == slot; });

  if (const auto tex = m_textures.find(slot); tex != m_textures.end()) {
    m_textures.erase(tex);
  }

  for (auto c = m_cache.begin(); c != m_cache.end(); ++c) {
    if (c->second == slot) {
      m_cache.erase(c);
      break;
    }
  }

  m_textureDescriptors[slot] = vk::DescriptorImageInfo(
    m_sampler.get(), m_placeholder->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
  m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot, m_textureDescriptors[slot]);
}

void TextureManager::drawUI() {
  if (ImGui::Begin("Texture Browser")) {
    static unsigned int selected = -1; {
      ImGui::BeginChild("Slots", ImVec2(ImGui::GetContentRegionAvail().x * 0.2f, 260), ImGuiChildFlags_None,
                        ImGuiWindowFlags_HorizontalScrollbar);
      for (const auto &id: m_textures | std::views::keys) {
        if (ImGui::Selectable(std::format("Slot: {}", id).c_str(), selected == id)) {
          selected = id;
        }
      }
      ImGui::EndChild();
    }
    ImGui::SameLine();

    ImGui::BeginChild("Preview", ImVec2(0, 260));
    if (selected != -1) {
      if (const auto tex = getTexture(selected); tex.has_value()) {
        float scale = 1.0f;
        const auto width = static_cast<float>(tex.value()->width);
        const auto height = static_cast<float>(tex.value()->height);
        if (width > height) {
          scale = 256.0f / width;
        } else {
          scale = 256.0f / height;
        }

        ImVec2 previewSize(width * scale, height * scale);
        ImGui::Image(tex.value()->getImGuiID(), previewSize);
      }
    } else {
      ImGui::Text("Select a slot...");
    }
    ImGui::EndChild();
  }
  ImGui::End();
}
