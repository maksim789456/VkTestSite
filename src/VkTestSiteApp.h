#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <tracy/Tracy.hpp>

#include "utils.cpp"
#include <string>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <set>
#include "QueueFamilyIndices.cpp"
#include "Swapchain.h"
#include "ShaderModule.h"

#ifndef VkTestSite_App_HPP
#define VkTestSite_App_HPP

class VkTestSiteApp {
public:
  void run();

private:
  GLFWwindow *m_window = nullptr;
  vk::Instance m_instance;
#ifndef NDEBUG
  vk::DebugUtilsMessengerEXT m_debugMessenger;
  vk::detail::DispatchLoaderDynamic m_didl;
#endif
  vk::UniqueSurfaceKHR m_surface;
  vk::PhysicalDevice m_physicalDevice;
  vk::Device m_device;
  vk::Queue m_graphicsQueue;
  vk::Queue m_presentQueue;
  Swapchain m_swapchain;
  vk::RenderPass m_renderPass;
  vk::PipelineLayout m_pipelineLayout;
  vk::Pipeline m_graphicsPipeline;
  vk::CommandPool m_commandPool;

  std::vector<vk::Framebuffer> m_framebuffers;
  std::vector<vk::CommandBuffer> m_commandBuffers;
  std::vector<vk::Fence> m_inFlight;
  std::vector<vk::Semaphore> m_imageAvailable;
  std::vector<vk::Semaphore> m_renderFinished;

  uint32_t m_currentFrame = 0;

  void initWindow();
  void initVk();
  void createInstance();
  void createLogicalDevice();
  void createQueues();
  void createRenderPass();
  void createPipeline();
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();

  void mainLoop();
  void render();
  void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);
  void recreateSwapchain();
  void cleanupSwapchain();
  void cleanup();
};

#endif
