#pragma once

#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include <stb_image.h>

#include "utils.cpp"
#include "BufferUtils.cpp"
#include <filesystem>
#include <cmath>
#include <algorithm>

class Texture {
public:
  Texture(
    vk::Device device,
    vma::Allocator allocator,
    uint32_t width, uint32_t height,
    uint32_t mipLevels,
    vk::Format format,
    vk::SampleCountFlagBits samples,
    vk::ImageAspectFlags aspects,
    vk::ImageUsageFlags usage,
    bool useSampler
  );

  static std::unique_ptr<Texture> createFromFile(
    vk::Device device,
    vma::Allocator allocator,
    vk::Queue queue,
    vk::CommandPool commandPool,
    const std::filesystem::path &path
  );

  vk::Image getImage() { return m_image.get(); };
  vk::ImageView getImageView() { return m_imageView.get(); };

private:
  vma::UniqueImage m_image;
  vma::UniqueAllocation m_imageAlloc;
  vk::UniqueImageView m_imageView;

  bool m_useSampler = false;
  vk::UniqueSampler m_sampler;
};

inline Texture::Texture(
  const vk::Device device,
  const vma::Allocator allocator,
  const uint32_t width, const uint32_t height,
  const uint32_t mipLevels,
  const vk::Format format,
  const vk::SampleCountFlagBits samples,
  const vk::ImageAspectFlags aspects,
  const vk::ImageUsageFlags usage,
  const bool useSampler
) {
  std::tie(m_image, m_imageAlloc) = createImageUnique(
    allocator,
    width, height, mipLevels,
    samples, format, vk::ImageTiling::eOptimal,
    usage, vk::MemoryPropertyFlagBits::eDeviceLocal
  );

  m_imageView = createImageViewUnique(device, m_image.get(), format, aspects, mipLevels);
  if (useSampler) {
    m_sampler = createSamplerUnique(device);
  }
}

inline std::unique_ptr<Texture> Texture::createFromFile(
  const vk::Device device,
  const vma::Allocator allocator,
  const vk::Queue queue,
  const vk::CommandPool commandPool,
  const std::filesystem::path &path
) {
  int width, height, channels;
  stbi_uc *origPixels = stbi_load(
    path.string().c_str(),
    &width,
    &height,
    &channels,
    STBI_rgb_alpha
  );
  if (!origPixels) {
    throw std::runtime_error("Failed to load texture image: " + path.string());
  }

  const vk::DeviceSize size = width * height * channels;
  auto mipLevels = static_cast<uint32_t>(
    std::floor(std::log2(std::max(width, height))) + 1
  );

  auto texture = std::make_unique<Texture>(
    device, allocator,
    width, height, mipLevels,
    vk::Format::eR8G8B8A8Srgb,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eColor,
    vk::ImageUsageFlagBits::eSampled
    | vk::ImageUsageFlagBits::eTransferSrc
    | vk::ImageUsageFlagBits::eTransferDst,
    false
  );

  transitionImageLayout(
    device, queue, commandPool,
    texture->m_image.get(),
    vk::Format::eR8G8B8A8Srgb,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal,
    mipLevels);

  auto [stagingBuffer, stagingBufferAlloc] = createBuffer(
    allocator,
    size,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
  );

  fillBufferRaw(allocator, stagingBufferAlloc, size, origPixels, size);
  stbi_image_free(origPixels);

  copyBufferToImage(
    device, queue, commandPool,
    stagingBuffer, texture->m_image.get(),
    width, height
  );
  allocator.destroyBuffer(stagingBuffer, stagingBufferAlloc);

  transitionImageLayout(
    device, queue, commandPool,
    texture->m_image.get(),
    vk::Format::eR8G8B8A8Srgb,
    vk::ImageLayout::eTransferDstOptimal,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    mipLevels
  );

  return texture;
}

#endif
