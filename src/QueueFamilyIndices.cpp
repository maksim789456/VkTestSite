#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

struct QueueFamilyIndices {
  uint32_t graphics;
  uint32_t present;
  uint32_t transfer;

  [[nodiscard]] bool isTransferQueueSeparated() const { return graphics != transfer; }

  QueueFamilyIndices() = default;

  QueueFamilyIndices(
    const vk::SurfaceKHR &surface,
    const vk::PhysicalDevice physical_device
  ) {
    auto families = physical_device.getQueueFamilyProperties();
    graphics = present = transfer = UINT32_MAX;

    for (uint32_t i = 0; i < families.size(); ++i) {
      auto family = families[i];
      if (family.queueFlags & vk::QueueFlagBits::eGraphics) {
        if (graphics == UINT32_MAX) graphics = i;
      }
      if (physical_device.getSurfaceSupportKHR(i, surface)) {
        if (present == UINT32_MAX) present = i;
      }
      if (family.queueCount == 1) continue;
      if (family.queueFlags & vk::QueueFlagBits::eTransfer) {
        if (transfer == UINT32_MAX) transfer = i;
      }
    }

    if (transfer == UINT32_MAX) {
      transfer = graphics;
    }
  }
};
