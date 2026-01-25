#pragma once

#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include <stb_image.h>
#include <imgui_impl_vulkan.h>

#include "utils.cpp"
#include "BufferUtils.cpp"
#include "StagingBuffer.h"
#include "TransferThread.h"
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
    const std::string &name = "Texture",
    const uint32_t arrayLayers = 1
  );

  ~Texture() {
    if (m_useSampler) {
      ImGui_ImplVulkan_RemoveTexture(m_imguiDS.release());
    }
  }

  void createImguiView() {
    ZoneScoped;
    if (m_useSampler) {
      auto imguiTextureDs = ImGui_ImplVulkan_AddTexture(
        m_sampler.get(), getImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      m_imguiDS = vk::UniqueDescriptorSet(imguiTextureDs);
    }
  }

  vk::Image getImage() { return m_image.get(); };
  vk::ImageView getImageView(const uint32_t mipLevel = 0) { return m_imageViews.at(mipLevel).get(); };
  vk::Sampler getSampler() { return m_sampler.get(); };
  ImTextureID getImGuiID() { return reinterpret_cast<ImTextureID>(static_cast<VkDescriptorSet>(m_imguiDS.get())); };
  const uint32_t width, height, mipLevels;

private:
  vma::UniqueImage m_image;
  vma::UniqueAllocation m_imageAlloc;
  std::vector<vk::UniqueImageView> m_imageViews;
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
  const std::string &name,
  const uint32_t arrayLayers
): width(width), height(height), mipLevels(mipLevels) {
  ZoneScoped;
  std::tie(m_image, m_imageAlloc) = createImageUnique(
    allocator,
    width, height, mipLevels,
    samples, format, vk::ImageTiling::eOptimal,
    usage, vk::MemoryPropertyFlagBits::eDeviceLocal,
    arrayLayers
  );
  setObjectName(device, m_image.get(), std::format("{} ", name));
  auto info = allocator.getAllocationInfo(m_imageAlloc.get());
  setObjectName(device, info.deviceMemory, std::format("{} memory", name));

  for (uint32_t mip = 0; mip < mipLevels; ++mip) {
    m_imageViews.emplace_back(createImageViewUnique(device, m_image.get(), format, aspects, mip, arrayLayers));
    setObjectName(device, getImageView(mip), std::format("{} view (mip = {})", name, mip));
  }

  if (useSampler) {
    m_sampler = createSamplerUnique(device);
    setObjectName(device, m_sampler.get(), std::format("{} sampler", name));
  }
}

#endif
