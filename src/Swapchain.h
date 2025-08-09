#pragma once

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include "utils.cpp"
#include "QueueFamilyIndices.cpp"
#include <vector>

#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

class Swapchain {
public:
  Swapchain();
  Swapchain(
    const vk::SurfaceKHR &surface,
    const vk::Device &device,
    const vk::PhysicalDevice &physical_device,
    GLFWwindow *window);
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
