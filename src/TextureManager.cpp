#include "TextureManager.h"

TextureManager::TextureManager(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  TextureWorkerPool &workerPool,
  DescriptorSet &descriptorSet,
  const uint32_t shaderBinding
): m_device(device), m_graphicsQueue(graphicsQueue), m_commandPool(commandPool), m_descriptorSet(&descriptorSet),
   m_workerPool(&workerPool), m_shaderBinding(shaderBinding) {
  m_sampler = createSamplerUnique(device);
}

uint32_t TextureManager::loadTextureFromFile(
  const std::filesystem::path &textureParent,
  const std::filesystem::path &filename,
  const vk::Format format
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
  spdlog::info(std::format("Push texture loading job: file {} at slot {}", filename.string(), slot));

  if (slot >= MAX_TEXTURE_PER_DESCRIPTOR) {
    throw std::runtime_error(
      std::format("Texture store full (limit = {})", MAX_TEXTURE_PER_DESCRIPTOR));
  }

  const auto textureJob = TextureLoadJob{
    .texIndex = slot,
    .filepath = texturePath,
  };

  m_workerPool->pushJob(textureJob);
  m_textures[slot] = nullptr;
  m_cache[filename.string()] = slot;

  return slot;
}

void TextureManager::checkTextureLoading() {
  if (TextureLoadDone loadDone{}; m_workerPool->tryDequeueDone(loadDone)) {
    ZoneScopedN("Loaded texture move");
    const auto slot = loadDone.job.texIndex;
    if (m_textures[slot] != nullptr)
      spdlog::warn(std::format("Try to move texture {} into occupied slot {}",
                               loadDone.job.filepath.string(), slot));

//TODO: gen mipmaps

    m_textures[slot] = std::move(loadDone.texture);
    m_textures[slot]->createImguiView();
    m_textureDescriptors[slot] = vk::DescriptorImageInfo(
      m_sampler.get(), m_textures[slot]->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
    m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot, m_textureDescriptors[slot]);
  }
}

void TextureManager::updateDS(DescriptorSet &descriptorSet) {
  m_descriptorSet = &descriptorSet;
  for (const auto &slot: m_textures) {
    if (slot.second == nullptr) continue;
    m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot.first, m_textureDescriptors.at(slot.first));
  }
}

std::optional<Texture *> TextureManager::getTexture(const uint32_t slot) {
  if (const auto tex = m_textures.find(slot); tex != m_textures.end()) {
    if (tex->second == nullptr) return std::nullopt;
    return tex->second.get();
  }

  return std::nullopt;
}

void TextureManager::unloadTexture(const uint32_t slot) {
  if (const auto tex = m_textures.find(slot); tex != m_textures.end()) {
    m_textures.erase(tex);
  }

  for (auto c = m_cache.begin(); c != m_cache.end(); ++c) {
    if (c->second == slot) {
      m_cache.erase(c);
      break;
    }
  }
}
