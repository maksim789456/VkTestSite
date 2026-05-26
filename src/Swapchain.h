#pragma once

#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#include "utils.cpp"
#include "QueueFamilyIndices.cpp"
#include <vector>
#include "core/VkContext.h"

class Swapchain {
public:
  Swapchain(const vkts::VkContext& context);
  void cmdSetViewport(vk::CommandBuffer cmdBuffer) const;
  void cmdSetScissor(vk::CommandBuffer cmdBuffer) const;
  void destroy(const vk::Device &device);

  vk::Format format;
  vk::Extent2D extent;
  vk::SwapchainKHR swapchain;
  std::vector<vk::Image> images;
  std::vector<vk::ImageView> imageViews;
};

#endif //SWAPCHAIN_H
