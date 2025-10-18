#pragma once

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include <tracy/Tracy.hpp>
#include <assimp/scene.h>
#include <glm/ext/matrix_float4x4.hpp>
#include "spdlog/spdlog.h"

#include <functional>
#include <iostream>
#include <optional>
#include <ranges>
#include <vector>
#include <spdlog/spdlog.h>

std::vector<char const *> static gatherLayers(
  std::vector<std::string> const &layers
#ifndef NDEBUG
  , std::vector<vk::LayerProperties> const &layerProperties
#endif
) {
  ZoneScoped;
  std::vector<char const *> enabledLayers;
  enabledLayers.reserve(layers.size());

#ifndef NDEBUG
  const auto layerAvailable = [&](const std::string_view name) {
    return std::ranges::any_of(layerProperties, [&](const vk::LayerProperties &lp) {
      return std::string_view(lp.layerName.data()) == name;
    });
  };
#endif

  for (const auto &layer: layers) {
#ifndef NDEBUG
    assert(layerAvailable(layer));
#endif
    enabledLayers.push_back(layer.data());
  }

#ifndef NDEBUG
  constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
  if (std::ranges::none_of(layers, [&](const std::string &l) { return l == validationLayer; })
      && layerAvailable(validationLayer)) {
    enabledLayers.push_back(validationLayer.data());
  }
#endif

  return enabledLayers;
}

std::vector<char const *> static gatherExtensions(
  std::vector<std::string> const &extensions
#ifndef NDEBUG
  , std::vector<vk::ExtensionProperties> const &extensionProperties
#endif
) {
  ZoneScoped;
  std::vector<char const *> enabledExtensions;
  enabledExtensions.reserve(extensions.size());

#ifndef NDEBUG
  const auto extensionAvailable = [&](const std::string_view name) {
    return std::ranges::any_of(extensionProperties, [&](const vk::ExtensionProperties &lp) {
      return std::string_view(lp.extensionName.data()) == name;
    });
  };
#endif

  for (const auto &extension: extensions) {
#ifndef NDEBUG
    if (!extensionAvailable(extension)) {
      std::cerr << "Extension " << extension << " not available" << std::endl;
      abort();
    }
#endif
    enabledExtensions.push_back(extension.data());
  }

#ifndef NDEBUG
  constexpr std::string_view debugUtilsExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  if (std::ranges::none_of(extensions, [&](const std::string &l) { return l == debugUtilsExtension; })
      && extensionAvailable(debugUtilsExtension)) {
    enabledExtensions.push_back(debugUtilsExtension.data());
  }
#endif

  return enabledExtensions;
}

VKAPI_ATTR vk::Bool32 static VKAPI_CALL debugUtilsMessangerCallback(
  vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
  const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
  void *pUserData
) {
  spdlog::level::level_enum level;
  switch (messageSeverity) {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
      level = spdlog::level::debug;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
      level = spdlog::level::info;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
      level = spdlog::level::warn;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
      level = spdlog::level::err;
      break;
    default:
      level = spdlog::level::info;
      break;
  }

  std::ostringstream oss;
  oss << vk::to_string(messageTypes)
      << " | ID: " << pCallbackData->messageIdNumber
      << " (" << (pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "no_name") << ")"
      << " | Message: " << (pCallbackData->pMessage ? pCallbackData->pMessage : "no_message");

  if (pCallbackData->queueLabelCount > 0) {
    oss << " | QueueLabels: ";
    for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++) {
      if (i > 0) oss << ", ";
      oss << pCallbackData->pQueueLabels[i].pLabelName;
    }
  }

  if (pCallbackData->cmdBufLabelCount > 0) {
    oss << " | CmdBufLabels: ";
    for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
      if (i > 0) oss << ", ";
      oss << pCallbackData->pCmdBufLabels[i].pLabelName;
    }
  }

  if (pCallbackData->objectCount > 0) {
    oss << " | Objects: ";
    for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
      if (i > 0) oss << "; ";
      oss << "{" << vk::to_string(pCallbackData->pObjects[i].objectType)
          << " handle=0x" << std::hex << pCallbackData->pObjects[i].objectHandle << std::dec;
      if (pCallbackData->pObjects[i].pObjectName) {
        oss << " name=" << pCallbackData->pObjects[i].pObjectName;
      }
      oss << "}";
    }
  }

  spdlog::log(level, oss.str());

  return vk::False;
}

#ifdef NDEBUG
vk::StructureChain<vk::InstanceCreateInfo>
#else
vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT>
#endif
static makeInstanceCreateInfoChain(
  vk::InstanceCreateFlagBits instanceCreateFlagBits,
  vk::ApplicationInfo const &applicationInfo,
  std::vector<const char *> const &layers,
  std::vector<const char *> const &extensions
) {
  ZoneScoped;
#ifdef NDEBUG
  vk::StructureChain<vk::InstanceCreateInfo> instanceCreateInfo({
    instanceCreateFlagBits, &applicationInfo, layers, extensions
  });
#else
  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

  vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> instanceCreateInfo(
    {instanceCreateFlagBits, &applicationInfo, layers, extensions},
    {{}, severityFlags, messageTypeFlags, &debugUtilsMessangerCallback}
  );
#endif
  return instanceCreateInfo;
}

std::optional<vk::PhysicalDevice> static pickPhysicalDevice(
  const vk::Instance &instance,
  const vk::SurfaceKHR &surface,
  const std::vector<const char *> &required_extensions
) {
  ZoneScoped;
  const std::vector<vk::PhysicalDevice> physical_devices = instance.enumeratePhysicalDevices();

  if (physical_devices.empty()) {
    std::cerr << "No GPU's devices found" << std::endl;
    return std::nullopt;
  }

  auto check_device_extensions = [&](const vk::PhysicalDevice &device) {
    auto available_extensions = device.enumerateDeviceExtensionProperties();

    return std::ranges::all_of(required_extensions, [&](const auto &required) {
      return std::ranges::any_of(available_extensions, [&](const auto &available) {
        return std::string_view(available.extensionName) == required;
      });
    });
  };

  auto check_device_suitability = [&](const vk::PhysicalDevice &device) {
    const auto queue_families = device.getQueueFamilyProperties();
    bool has_graphics = false;
    bool has_present = false;

    uint32_t idx = 0;
    for (auto const queue_family: queue_families) {
      has_graphics = has_graphics || (queue_family.queueFlags & vk::QueueFlagBits::eGraphics);

      if (surface) {
        has_present = has_present || device.getSurfaceSupportKHR(idx, surface);
      }

      ++idx;
    }

    return has_graphics && (!surface || has_present);
  };

  for (const auto &device: physical_devices) {
    if (check_device_extensions(device) && check_device_suitability(device)) {
      return device;
    }
  }

  std::cerr << "Failed to found a suitable GPU!" << std::endl;
  return std::nullopt;
}

static vk::SampleCountFlagBits findMaxMsaaSamples(
  const vk::PhysicalDevice &physical_device
) {
  const auto props = physical_device.getProperties();
  const auto counts =
      props.limits.framebufferColorSampleCounts
      & props.limits.framebufferDepthSampleCounts;

  for (int bit = static_cast<int>(vk::SampleCountFlagBits::e64);
       bit >= static_cast<int>(vk::SampleCountFlagBits::e1);
       bit >>= 1) {
    const auto sampleCount = static_cast<vk::SampleCountFlagBits>(bit);
    if (counts & sampleCount) {
      return sampleCount;
    }
  }

  return vk::SampleCountFlagBits::e1;
}

template<typename T>
static void setObjectName(
  const vk::Device device,
  const T &object,
  std::string name
) {
#ifndef NDEBUG
  const vk::DebugUtilsObjectNameInfoEXT nameInfo{
    T::objectType,
    reinterpret_cast<uint64_t>(static_cast<typename T::CType>(object)),
    name.c_str()
  };

  device.setDebugUtilsObjectNameEXT(nameInfo);
#endif
}

template<typename Func>
static void executeSingleTimeCommands(
  const vk::Device device,
  const vk::Queue queue,
  const vk::CommandPool commandPool,
  const Func &&executor
) {
  const auto allocInfo = vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
  const auto cmd = device.allocateCommandBuffers(allocInfo).front();

  constexpr auto beginInfo = vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
  cmd.begin(beginInfo);
  executor(cmd);
  cmd.end();

  auto submitInfo = vk::SubmitInfo{};
  submitInfo.setCommandBufferCount(1);
  submitInfo.setPCommandBuffers(&cmd);

  queue.submit(submitInfo);
  queue.waitIdle();

  device.freeCommandBuffers(commandPool, cmd);
}

static glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4 &m) {
  return glm::mat4(
    m.a1, m.b1, m.c1, m.d1,
    m.a2, m.b2, m.c2, m.d2,
    m.a3, m.b3, m.c3, m.d3,
    m.a4, m.b4, m.c4, m.d4
  );
}

static std::pair<vma::UniqueImage, vma::UniqueAllocation> createImageUnique(
  const vma::Allocator allocator,
  const uint32_t width, const uint32_t height,
  const uint32_t mipLevels,
  const vk::SampleCountFlagBits samples,
  const vk::Format format,
  const vk::ImageTiling tiling,
  const vk::ImageUsageFlags usage,
  const vk::MemoryPropertyFlags properties
) {
  auto info = vk::ImageCreateInfo(
    {}, vk::ImageType::e2D,
    format, vk::Extent3D(width, height, 1),
    mipLevels, 1, samples,
    tiling, usage, vk::SharingMode::eExclusive
  );
  info.setInitialLayout(vk::ImageLayout::eUndefined);

  const auto allocInfo = vma::AllocationCreateInfo({}, vma::MemoryUsage::eAutoPreferDevice, properties);
  return allocator.createImageUnique(info, allocInfo);
}

static vk::UniqueSampler createSamplerUnique(const vk::Device device) {
  auto info = vk::SamplerCreateInfo();
  info.setMagFilter(vk::Filter::eLinear)
      .setMinFilter(vk::Filter::eLinear)
      .setAddressModeU(vk::SamplerAddressMode::eRepeat)
      .setAddressModeV(vk::SamplerAddressMode::eRepeat)
      .setAddressModeW(vk::SamplerAddressMode::eRepeat)
      .setAnisotropyEnable(true)
      .setMaxAnisotropy(16.0)
      .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
      .setUnnormalizedCoordinates(false)
      .setCompareEnable(false)
      .setCompareOp(vk::CompareOp::eAlways)
      .setMipmapMode(vk::SamplerMipmapMode::eLinear)
      .setMipLodBias(0.0f)
      .setMinLod(0.0f)
      .setMaxLod(0.0f);
  return device.createSamplerUnique(info);
}

static vk::UniqueImageView createImageViewUnique(
  const vk::Device device,
  const vk::Image image,
  const vk::Format format,
  const vk::ImageAspectFlags aspect,
  const uint32_t mipLevels
) {
  const auto subresource = vk::ImageSubresourceRange(aspect, 0, mipLevels, 0, 1);
  const auto info = vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, format, {}, subresource);
  return device.createImageViewUnique(info);
}

static void cmdTransitionImageLayout(
  const vk::CommandBuffer commandBuffer,
  const vk::Image image,
  const vk::ImageLayout oldLayout,
  const vk::ImageLayout newLayout,
  const uint32_t mipLevels,
  const vk::Format format = vk::Format::eR32G32B32A32Sfloat,
  const uint32_t baseMipLevel = 0
) {
  auto [srcAccessMask, dstAccessMask, srcStageMask, dstStageMask] = [oldLayout, newLayout]()
    -> std::tuple<vk::AccessFlags, vk::AccessFlags, vk::PipelineStageFlags, vk::PipelineStageFlags> {
        using AF = vk::AccessFlagBits;
        using PF = vk::PipelineStageFlagBits;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
          return {{}, AF::eTransferWrite, PF::eTopOfPipe, PF::eTransfer};
        }
        if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eTransferSrcOptimal) {
          return {AF::eTransferWrite, AF::eTransferRead, PF::eTransfer, PF::eTransfer};
        }
        if (oldLayout == vk::ImageLayout::eTransferSrcOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
          return {AF::eTransferRead, AF::eShaderRead, PF::eTransfer, PF::eFragmentShader};
        }
        if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
          return {AF::eTransferWrite, AF::eShaderRead, PF::eTransfer, PF::eFragmentShader};
        }
        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
          return {
            {}, AF::eDepthStencilAttachmentRead | AF::eDepthStencilAttachmentWrite,
            PF::eTopOfPipe, PF::eEarlyFragmentTests
          };
        }
        throw std::runtime_error("Unsupported image layout transition!");
      }();

  vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
  if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
    if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
      aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    } else {
      aspectMask = vk::ImageAspectFlagBits::eDepth;
    }
  }

  const auto subresource = vk::ImageSubresourceRange(aspectMask, baseMipLevel, mipLevels, 0, 1);
  const auto barrier = vk::ImageMemoryBarrier(
    srcAccessMask, dstAccessMask,
    oldLayout, newLayout,
    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
    image, subresource
  );
  commandBuffer.pipelineBarrier(srcStageMask, dstStageMask, {}, {}, {}, barrier);
}

static void cmdTransitionImageLayout2(
  const vk::CommandBuffer commandBuffer,
  const vk::Image &image,
  const vk::ImageLayout oldLayout,
  const vk::ImageLayout newLayout,
  const vk::ImageSubresourceRange &subresource
) {
  auto [srcAccessMask, dstAccessMask, srcStageMask, dstStageMask] = [oldLayout, newLayout]()
    -> std::tuple<vk::AccessFlagBits2, vk::AccessFlagBits2, vk::PipelineStageFlagBits2, vk::PipelineStageFlagBits2> {
        using AF2 = vk::AccessFlagBits2;
        using PF2 = vk::PipelineStageFlagBits2;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
          return {{}, AF2::eTransferWrite, PF2::eTopOfPipe, PF2::eTransfer};
        }
        if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
          return {AF2::eTransferWrite, AF2::eShaderRead, PF2::eTransfer, PF2::eFragmentShader};
        }
        if (oldLayout == vk::ImageLayout::eDepthStencilReadOnlyOptimal
            && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
          return {AF2::eDepthStencilAttachmentWrite, AF2::eShaderRead, PF2::eLateFragmentTests, PF2::eComputeShader};
        }
        if (oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
          return {AF2::eShaderRead, AF2::eDepthStencilAttachmentWrite, PF2::eComputeShader, PF2::eEarlyFragmentTests};
        }
        throw std::runtime_error("Unsupported image layout transition!");
      }();

  const auto barrier = vk::ImageMemoryBarrier2(
    srcStageMask, srcAccessMask,
    dstStageMask, dstAccessMask,
    oldLayout, newLayout,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
    image, subresource
  );
  auto dep = vk::DependencyInfo();
  dep.setImageMemoryBarriers(barrier);
  commandBuffer.pipelineBarrier2(dep);
}

static void transitionImageLayout(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  const vk::Image image,
  const vk::Format format,
  const vk::ImageLayout oldLayout,
  const vk::ImageLayout newLayout,
  const uint32_t mipLevels
) {
  executeSingleTimeCommands(device, graphicsQueue, commandPool, [&](const vk::CommandBuffer cmd) {
    cmdTransitionImageLayout(cmd, image, oldLayout, newLayout, mipLevels, format);
  });
}

static void generateMipmaps(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  const vk::Image image,
  const uint32_t width, const uint32_t height,
  const uint32_t mipLevels
) {
  executeSingleTimeCommands(device, graphicsQueue, commandPool, [&](const vk::CommandBuffer cmd) {
    auto mipWidth = width;
    auto mipHeight = height;
    for (int mipLevel = 1; mipLevel < mipLevels; ++mipLevel) {
      cmdTransitionImageLayout(
        cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
        1, {}, mipLevel - 1);

      const auto srcSubResource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mipLevel - 1, 0, 1);
      const auto dstSubResource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1);
      auto blit = vk::ImageBlit(
        srcSubResource,
        {
          vk::Offset3D(0, 0, 0),
          vk::Offset3D(mipWidth, mipHeight, 1),
        },
        dstSubResource,
        {
          vk::Offset3D(0, 0, 0),
          vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1),
        }
      );
      cmd.blitImage(
        image, vk::ImageLayout::eTransferSrcOptimal,
        image, vk::ImageLayout::eTransferDstOptimal,
        blit, vk::Filter::eLinear
      );

      cmdTransitionImageLayout(
        cmd, image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        1, {}, mipLevel - 1);

      mipWidth = mipWidth > 1 ? mipWidth / 2 : mipWidth;
      mipHeight = mipHeight > 1 ? mipHeight / 2 : mipHeight;
    }

    cmdTransitionImageLayout(
      cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
      1, {}, mipLevels - 1);
  });
}
