#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

struct QueueFamilyIndices {
  std::optional<uint32_t> graphics;
  std::optional<uint32_t> present;
  std::optional<uint32_t> transfer;

  [[nodiscard]] bool isTransferQueueSeparated() const {
    return graphics && transfer && *graphics != *transfer;
  }

  QueueFamilyIndices() = delete;

  QueueFamilyIndices(
    const vk::SurfaceKHR &surface,
    const vk::PhysicalDevice physical_device
  ) {
    const auto families = physical_device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < families.size(); ++i) {
      const auto family = families[i];
      const auto flags = family.queueFlags;

      if (!graphics
          && flags & vk::QueueFlagBits::eGraphics) {
        graphics = i;
      }
      if (!present
          && physical_device.getSurfaceSupportKHR(i, surface)) {
        present = i;
      }

      // Whenever possible, we try to achieve a separate family for transfer queue
      if (!transfer && graphics
        && *graphics != i
        && flags & vk::QueueFlagBits::eTransfer) {
        transfer = i;
      }
    }

    if (!transfer) {
      transfer = graphics;
    }
  }
};
