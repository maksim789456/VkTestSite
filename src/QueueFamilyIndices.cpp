#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

struct QueueFamilyIndices {
  uint32_t graphics;
  uint32_t present;

  QueueFamilyIndices() : graphics(-1), present(-1) {}

  QueueFamilyIndices(
    const vk::SurfaceKHR &surface,
    const vk::PhysicalDevice physical_device
  ) {
    auto props = physical_device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < props.size(); ++i) {
      if (props[i].queueFlags & vk::QueueFlagBits::eGraphics) {
        graphics = i;
      }
    }

    for (uint32_t i = 0; i < props.size(); ++i) {
      if (physical_device.getSurfaceSupportKHR(i, surface)) {
        present = i;
      }
    }
  }
};
