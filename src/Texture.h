#pragma once

#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include <stb_image.h>
#include <imgui_impl_vulkan.h>

#include "utils.cpp"
#include "BufferUtils.cpp"
#include <filesystem>
#include <cmath>
#include <algorithm>

#define MAX_TEXTURE_PER_DESCRIPTOR 64

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
    bool useSampler = false,
    const std::string &name = "Texture"
  );

  ~Texture() {
    if (m_useSampler) {
      ImGui_ImplVulkan_RemoveTexture(m_imguiDS.release());
    }
  }

  static std::unique_ptr<Texture> createFromFile(
    vk::Device device,
    vma::Allocator allocator,
    vk::Queue queue,
    vk::CommandPool commandPool,
    const std::filesystem::path &path,
    vk::Format format = vk::Format::eR8G8B8A8Unorm
  );

  vk::Image getImage() { return m_image.get(); };
  vk::ImageView getImageView() { return m_imageView.get(); };
  ImTextureID getImGuiID() { return reinterpret_cast<ImTextureID>(static_cast<VkDescriptorSet>(m_imguiDS.get())); };
  const uint32_t width, height, mipLevels;

private:
  vma::UniqueImage m_image;
  vma::UniqueAllocation m_imageAlloc;
  vk::UniqueImageView m_imageView;
  vk::UniqueDescriptorSet m_imguiDS;

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
  const bool useSampler,
  const std::string &name
): width(width), height(height), mipLevels(mipLevels) {
  std::tie(m_image, m_imageAlloc) = createImageUnique(
    allocator,
    width, height, mipLevels,
    samples, format, vk::ImageTiling::eOptimal,
    usage, vk::MemoryPropertyFlagBits::eDeviceLocal
  );
  setObjectName(device, m_image.get(), std::format("{} ", name));
  auto info = allocator.getAllocationInfo(m_imageAlloc.get());
  setObjectName(device, info.deviceMemory, std::format("{} memory", name));

  m_imageView = createImageViewUnique(device, m_image.get(), format, aspects, mipLevels);
  setObjectName(device, m_imageView.get(), std::format("{} view", name));
  if (useSampler) {
    m_sampler = createSamplerUnique(device);
    setObjectName(device, m_sampler.get(), std::format("{} sampler", name));

    auto imguiTextureDs = ImGui_ImplVulkan_AddTexture(
      m_sampler.get(), m_imageView.get(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_imguiDS = vk::UniqueDescriptorSet(imguiTextureDs);
  }
}

inline std::unique_ptr<Texture> Texture::createFromFile(
  const vk::Device device,
  const vma::Allocator allocator,
  const vk::Queue queue,
  const vk::CommandPool commandPool,
  const std::filesystem::path &path,
  const vk::Format format
) {
  int width, height, channels;
  stbi_uc *origPixels = stbi_load(
    path.string().c_str(),
    &width,
    &height,
    &channels,
    0
  );
  if (!origPixels) {
    throw std::runtime_error("Failed to load texture image: " + path.string());
  }

  std::vector<uint8_t> pixels(width * height * 4);
  const vk::DeviceSize originalSize = width * height * channels;
  auto mipLevels = static_cast<uint32_t>(
    std::floor(std::log2(std::max(width, height))) + 1
  );

  if (channels == 4) {
    memcpy(pixels.data(), origPixels, originalSize);
  } else if (channels == 3) {
    for (int i = 0; i < width * height; ++i) {
      pixels[i * 4 + 0] = origPixels[i * 3 + 0];
      pixels[i * 4 + 1] = origPixels[i * 3 + 1];
      pixels[i * 4 + 2] = origPixels[i * 3 + 2];
      pixels[i * 4 + 3] = 255;
    }
  }
  stbi_image_free(origPixels);

  auto texture = std::make_unique<Texture>(
    device, allocator,
    width, height, mipLevels,
    format,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eColor,
    vk::ImageUsageFlagBits::eSampled
    | vk::ImageUsageFlagBits::eTransferSrc
    | vk::ImageUsageFlagBits::eTransferDst,
    true,
    path.filename().string()
  );

  transitionImageLayout(
    device, queue, commandPool,
    texture->m_image.get(),
    format,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal,
    mipLevels);

  auto [stagingBuffer, stagingBufferAlloc] = createBuffer(
    allocator,
    pixels.size(),
    vk::BufferUsageFlagBits::eTransferSrc,
    vma::MemoryUsage::eCpuToGpu, vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
  );

  fillBuffer(allocator, stagingBufferAlloc, pixels.size(), pixels);

  copyBufferToImage(
    device, queue, commandPool,
    stagingBuffer, texture->m_image.get(),
    width, height
  );
  allocator.destroyBuffer(stagingBuffer, stagingBufferAlloc);

  return texture;
}

#endif
