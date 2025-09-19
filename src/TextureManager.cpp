#include "TextureManager.h"

TextureManager::TextureManager(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  StagingBuffer &stagingBuffer,
  TransferThread &transferThread,
  const vma::Allocator allocator,
  DescriptorSet &descriptorSet,
  const uint32_t shaderBinding
): m_device(device), m_graphicsQueue(graphicsQueue), m_commandPool(commandPool), m_stagingBuffer(&stagingBuffer), m_transferThread(&transferThread),
   m_allocator(allocator), m_descriptorSet(&descriptorSet), m_shaderBinding(shaderBinding) {
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
    std::cout << std::format("Reuse texture {} from {}", filename.string(), it->second) << "\n";
    return it->second;
  }

  uint32_t slot = 0;
  for (; slot < MAX_TEXTURE_PER_DESCRIPTOR; ++slot) {
    if (!m_textures.contains(slot)) {
      break;
    }
  }
  std::cout << std::format("Loading texture {} at {}", filename.string(), slot) << "\n";

  if (slot >= MAX_TEXTURE_PER_DESCRIPTOR) {
    throw std::runtime_error(
      std::format("Texture store full (limit = {})", MAX_TEXTURE_PER_DESCRIPTOR));
  }

  auto texture = Texture::createFromFile(
    m_device,
    m_allocator,
    *m_stagingBuffer,
    *m_transferThread,
    texturePath,
    format
  );

  /*generateMipmaps(
    m_device,
    m_graphicsQueue,
    m_commandPool,
    texture->getImage(),
    texture->width,
    texture->height,
    texture->mipLevels
  );*/

  m_textures[slot] = std::move(texture);
  m_cache[filename.string()] = slot;
  m_textureDescriptors[slot] = vk::DescriptorImageInfo(
    m_sampler.get(), m_textures[slot]->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
  m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot, m_textureDescriptors[slot]);

  return slot;
}

void TextureManager::updateDS(DescriptorSet& descriptorSet) {
  m_descriptorSet = &descriptorSet;
  for (const auto &slot: m_textures | std::views::keys)
    m_descriptorSet->updateTexture(m_device, m_shaderBinding, slot, m_textureDescriptors.at(slot));
}

std::optional<Texture *> TextureManager::getTexture(const uint32_t slot) {
  if (const auto tex = m_textures.find(slot); tex != m_textures.end()) {
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
