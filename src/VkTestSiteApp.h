#pragma once

#ifndef VkTestSite_App_HPP
#define VkTestSite_App_HPP

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include "ImGUIStyle.h"
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include "tinyfiledialogs/tinyfiledialogs.h"

#include "utils.cpp"
#include <string>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <set>

#include "QueueFamilyIndices.cpp"
#include "Swapchain.h"
#include "ShaderModule.h"
#include "Vertex.h"
#include "DescriptorPool.h"
#include "DescriptorSet.h"
#include "Model.h"
#include "Ubo.h"
#include "Camera.h"
#include "TextureManager.h"
#include "Pipeline.h"
#include "Light.h"
#include "StagingBuffer.h"
#include "TextureWorkersPool.h"
#include "TransferThread.h"

#include <vr/XrSystem.h>

struct alignas(16) UniformBufferObject {
  glm::vec4 viewPos;
  glm::mat4 viewProj[2];
  glm::mat4 invViewProj[2];
  uint32_t displayDebugTarget;
};

class VkTestSiteApp {
public:
  void run();

private:
  GLFWwindow *m_window = nullptr;
  tracy::VkCtx *m_vkContext = nullptr;
  vk::CommandBuffer m_tracyCmdBuffer;

  vk::detail::DynamicLoader m_loader;
  vk::Instance m_instance;
  vma::Allocator m_allocator = nullptr;
#ifndef NDEBUG
  vk::DebugUtilsMessengerEXT m_debugMessenger;
#endif
  vk::UniqueSurfaceKHR m_surface;
  vk::PhysicalDevice m_physicalDevice;
  vk::SampleCountFlagBits m_msaaSamples = vk::SampleCountFlagBits::e1;
  vk::Device m_device;
  vk::Queue m_graphicsQueue;
  vk::Queue m_presentQueue;
  Swapchain m_swapchain;
  vk::RenderPass m_renderPass;
  vk::Pipeline m_geometryPipeline;
  vk::Pipeline m_lightingPipeline;
  vk::CommandPool m_commandPool;
  DescriptorPool m_descriptorPool;
  DescriptorSet m_geometryDescriptorSet;
  DescriptorSet m_lightingDescriptorSet;
  std::unique_ptr<Texture> m_depth;
  std::unique_ptr<Texture> m_albedo;
  std::unique_ptr<Texture> m_normal;
  std::unique_ptr<Camera> m_camera;

  std::unique_ptr<Model> m_model;
  bool m_modelLoaded = false;
  std::unique_ptr<TextureManager> m_texManager;
  std::unique_ptr<LightManager> m_lightManager;

  vk::Queue m_transferQueue;
  std::unique_ptr<TransferThread> m_transferThread;
  std::unique_ptr<StagingBuffer> m_stagingBuffer;
  std::unique_ptr<TextureWorkerPool> m_textureWorkerPool;

  std::unique_ptr<vr::XrSystem> m_xrSystem;

  std::vector<vk::Framebuffer> m_framebuffers;
  std::vector<UniformBuffer<UniformBufferObject>> m_uniforms = {};
  std::vector<vk::CommandBuffer> m_commandBuffers;
  std::vector<vk::UniqueCommandBuffer> m_imguiCommandBuffers;
  std::vector<vk::UniqueCommandBuffer> m_lightingCommandBuffers;
  std::vector<vk::Fence> m_inFlight;
  std::vector<vk::Semaphore> m_imageAvailable;
  std::vector<vk::Semaphore> m_renderFinished;

  uint32_t m_currentFrame = 0;
  int32_t m_debugView = 0;
  float m_lastTime = 0.0f;

  void initWindow();
  void initVk();
  void createInstance();
  void createLogicalDevice();
  void createQueues();
  void createRenderPass();
  void createPipeline();
  void createColorObjets();
  void createDepthObjets();
  void createFramebuffers();
  void createUniformBuffers();
  void createDescriptorSet();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();

  void mainLoop();
  void render(ImDrawData* draw_data, float deltaTime);
  void updateUniformBuffer(uint32_t imageIndex);
  void recordCommandBuffer(ImDrawData* draw_data, const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);
  void recreateSwapchain();
  void cleanupSwapchain();
  void cleanup();
};

#endif
