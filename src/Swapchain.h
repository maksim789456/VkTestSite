#pragma once

#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#include "utils.cpp"
#include "QueueFamilyIndices.cpp"
#include <vector>
#include "core/VkContext.h"

class Swapchain {
public:
  explicit Swapchain(const vkts::VkContext &context);

  /**
   * Acquire next swapchain image
   * @return Pair isSwapchainDirty/imageIndex
   */
  std::pair<bool, uint32_t> acquireNextImageKHR(
    vk::Semaphore semaphore = nullptr,
    vk::Fence fence = nullptr
  );

  /**
   * Present current image
   * @return isSwapchainDirty
   */
  bool present(vk::Semaphore semaphore = nullptr);

  void cmdSetViewport(vk::CommandBuffer cmdBuffer) const;

  void cmdSetScissor(vk::CommandBuffer cmdBuffer) const;

  void destroy(const vk::Device &device);

  vk::Format format;
  vk::Extent2D extent;
  vk::SwapchainKHR swapchain;
  std::vector<vk::Image> images;
  std::vector<vk::ImageView> imageViews;
  uint32_t imageIndex = -1;

private:
  const vkts::VkContext &m_context;
};

#endif //SWAPCHAIN_H
