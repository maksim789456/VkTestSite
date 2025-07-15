#include "VkTestSiteApp.h"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define MAX_FRAME_IN_FLIGHT 2 //0..2 -> 3 frames
#define MAX_TEXTURE_PER_DESCRIPTOR 64

const std::vector DEVICE_EXTENSIONS = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
};

const std::vector LAYERS = {
#ifndef NDEBUG
  "VK_LAYER_KHRONOS_validation"
#endif
};

void VkTestSiteApp::run() {
  ZoneScoped;
  initWindow();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::ApplyCurrentStyle();
  initVk();
  mainLoop();

  m_device.waitIdle();
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  cleanup();
  glfwDestroyWindow(m_window);
  glfwTerminate();
}

void VkTestSiteApp::initWindow() {
  ZoneScoped;
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  m_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "VK test", nullptr, nullptr);
}

void VkTestSiteApp::initVk() {
  ZoneScoped;
  createInstance();

  VkSurfaceKHR surface_tmp;
  glfwCreateWindowSurface(m_instance, m_window, nullptr, &surface_tmp);
  m_surface = vk::UniqueSurfaceKHR(surface_tmp, m_instance);
  const auto deviceTmp = pickPhysicalDevice(m_instance, m_surface.get(), DEVICE_EXTENSIONS);
  if (!deviceTmp) {
    abort();
  }
  m_physicalDevice = *deviceTmp;
  createLogicalDevice();
  createQueues();

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.flags = {};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.physicalDevice = m_physicalDevice;
  allocatorInfo.device = m_device;
  allocatorInfo.instance = m_instance;
  auto allocCreateResult = vmaCreateAllocator(&allocatorInfo, &m_allocator);
  if (allocCreateResult != VK_SUCCESS) {
    std::cerr << "vmaCreateAllocator failed with error code: " << allocCreateResult << std::endl;
    throw std::runtime_error("Failed to create VMA allocator");
  }

  m_swapchain = Swapchain(m_surface.get(), m_device, m_physicalDevice, m_window);
  createRenderPass();
  createPipeline();
  m_descriptorPool = DescriptorPool(m_device);
  //createDescriptorSet();
  createFramebuffers();
  createCommandPool();
  createCommandBuffers();
  createSyncObjects();

  ImGui_ImplGlfw_InitForVulkan(m_window, true);
  const auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);
  ImGui_ImplVulkan_InitInfo vkInitInfo = {};
  vkInitInfo.ApiVersion = VK_API_VERSION_1_3;
  vkInitInfo.Instance = m_instance;
  vkInitInfo.PhysicalDevice = m_physicalDevice;
  vkInitInfo.Device = m_device;
  vkInitInfo.QueueFamily = indices.graphics;
  vkInitInfo.Queue = m_graphicsQueue;
  vkInitInfo.RenderPass = m_renderPass;
  vkInitInfo.MinImageCount = vkInitInfo.ImageCount = MAX_FRAME_IN_FLIGHT;
  vkInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  vkInitInfo.Subpass = 0;
  vkInitInfo.DescriptorPoolSize = 100;
  if (!ImGui_ImplVulkan_Init(&vkInitInfo)) {
    std::cerr << "Failed to initialize Imgui Vulkan render" << std::endl;
    abort();
  }
}

void VkTestSiteApp::createInstance() {
  ZoneScoped;

  constexpr vk::ApplicationInfo app_info(
    "VK Test Site", 1,
    "Some VK bullshit", 1,
    VK_API_VERSION_1_3
  );

  uint32_t glfw_extension_count;
  const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

  std::vector<std::string> required_extensions;
  const std::vector<std::string> required_layers;

  for (uint32_t i = 0; i < glfw_extension_count; ++i) {
    required_extensions.emplace_back(glfw_extensions[i]);
  }

  //required_extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

  const auto enabled_extensions = gatherExtensions(required_extensions, vk::enumerateInstanceExtensionProperties());
  const auto enabled_layers = gatherLayers(required_layers, vk::enumerateInstanceLayerProperties());

  //vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR
  auto create_info = makeInstanceCreateInfoChain({}, app_info,
                                                 enabled_layers, enabled_extensions);
  m_instance = vk::createInstance(create_info.get<vk::InstanceCreateInfo>());

#ifndef NDEBUG
  m_didl = vk::detail::DispatchLoaderDynamic(m_instance, vkGetInstanceProcAddr);
  m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(create_info.get<vk::DebugUtilsMessengerCreateInfoEXT>(),
                                                             nullptr, m_didl);
#endif
}

void VkTestSiteApp::createQueues() {
  ZoneScoped;
  const auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);
  m_graphicsQueue = m_device.getQueue(indices.graphics, 0);
  m_presentQueue = m_device.getQueue(indices.present, 0);
}

void VkTestSiteApp::createLogicalDevice() {
  ZoneScoped;
  auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);

  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
  std::set queue_families = {indices.graphics, indices.present};

  float queuePriority = 1.0f;
  for (uint32_t queue_family: queue_families) {
    vk::DeviceQueueCreateInfo queue_create_info{};
    queue_create_info
        .setQueueFamilyIndex(queue_family)
        .setQueueCount(1)
        .setPQueuePriorities(&queuePriority);
    queue_create_infos.push_back(queue_create_info);
  }

  vk::PhysicalDeviceFeatures device_features{};
  vk::PhysicalDeviceVulkan12Features vulkan12_features{};
  vulkan12_features
      .setHostQueryReset(true)
      .setDescriptorIndexing(true);
  device_features
      .setSamplerAnisotropy(true)
      .setSampleRateShading(true);

  vk::DeviceCreateInfo device_create_info(
    {},
    queue_create_infos,
    LAYERS,
    DEVICE_EXTENSIONS,
    &device_features
  );
  device_create_info.pNext = &vulkan12_features;

  m_device = m_physicalDevice.createDevice(device_create_info);
}

void VkTestSiteApp::createRenderPass() {
  ZoneScoped;
  const auto colorAttachment = vk::AttachmentDescription(
    {}, m_swapchain.format, vk::SampleCountFlagBits::e1,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
  constexpr auto colorAttachmentRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);

  std::vector<vk::AttachmentReference> inputAttachmentsRefs = {};
  std::vector colorAttachmentsRefs = {colorAttachmentRef};
  auto subpass = vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, inputAttachmentsRefs,
                                        colorAttachmentsRefs);

  std::vector attachments = {colorAttachment};
  std::vector subpasses = {subpass};
  const auto renderPassInfo = vk::RenderPassCreateInfo({}, attachments, subpass);

  m_renderPass = m_device.createRenderPass(renderPassInfo);
}

void VkTestSiteApp::createPipeline() {
  ZoneScoped;
  auto shaderModule = ShaderModule();
  shaderModule.load("../res/shaders/test.slang.spv");
  shaderModule.compile(m_device);
  std::vector shaderStages = {
    shaderModule.vertexPipelineInfo, shaderModule.fragmentPipelineInfo
  };

  std::vector dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
  };

  auto bindingDescription = Vertex::GetBindingDescription();
  auto attributeDescription = Vertex::GetAttributeDescriptions();
  auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo({}, dynamicStates);
  auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo({}, bindingDescription, attributeDescription); //TODO
  auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList, false);
  auto viewportState = vk::PipelineViewportStateCreateInfo({}, 1, nullptr, 1, nullptr);
  auto rasterizer = vk::PipelineRasterizationStateCreateInfo(
    {}, false, false, vk::PolygonMode::eFill,
    vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise,
    false, {}, {}, {}, 1.0f);
  auto multisampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, false);
  auto colorAttachment = vk::PipelineColorBlendAttachmentState(false);
  colorAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  std::vector colorAttachments = {colorAttachment};
  auto colorBlend = vk::PipelineColorBlendStateCreateInfo({}, false, vk::LogicOp::eCopy, colorAttachments);

  auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo({}, 0, nullptr, 0, nullptr);
  m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo); //TODO

  auto pipelineInfo = vk::GraphicsPipelineCreateInfo({});
  pipelineInfo.setStages(shaderStages)
      .setPVertexInputState(&vertexInputInfo)
      .setPInputAssemblyState(&inputAssembly)
      .setPViewportState(&viewportState)
      .setPRasterizationState(&rasterizer)
      .setPMultisampleState(&multisampling)
      .setPColorBlendState(&colorBlend)
      .setPDynamicState(&dynamicStateInfo)
      .setLayout(m_pipelineLayout)
      .setRenderPass(m_renderPass)
      .setSubpass(0)
      .setBasePipelineHandle(VK_NULL_HANDLE)
      .setBasePipelineIndex(-1);

  auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
  if (result.result != vk::Result::eSuccess) {
    std::cerr << "Failed to create graphics pipeline" << std::endl;
    abort();
  }
  m_graphicsPipeline = result.value;

  shaderModule.destroy(m_device);
}

void VkTestSiteApp::createFramebuffers() {
  ZoneScoped;
  m_framebuffers.resize(m_swapchain.imageViews.size());
  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    std::vector attachments = {m_swapchain.imageViews[i]};

    auto framebufferInfo = vk::FramebufferCreateInfo(
      {}, m_renderPass, attachments,
      m_swapchain.extent.width, m_swapchain.extent.height, 1
    );
    m_framebuffers[i] = m_device.createFramebuffer(framebufferInfo);
  }
}

void VkTestSiteApp::createDescriptorSet() {
  const auto layouts = std::vector{
    DescriptorLayout{
      .type = vk::DescriptorType::eUniformBuffer,
      .stage = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
      .bindingFlags = {},
      .shaderBinding = 0,
      .count = 1,
      .imageInfos = {},
      .bufferInfos = {}
    },
    DescriptorLayout{
      .type = vk::DescriptorType::eCombinedImageSampler,
      .stage = vk::ShaderStageFlagBits::eFragment,
      .bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind,
      .shaderBinding = 1,
      .count = MAX_TEXTURE_PER_DESCRIPTOR,
      .imageInfos = {},
      .bufferInfos = {}
    }
  };
  const auto pushConsts = std::vector<vk::PushConstantRange>{};
  m_descriptorSet = DescriptorSet(m_device, m_descriptorPool.getDescriptorPool(), m_swapchain.imageViews.size(),
                                  layouts, pushConsts);
}

void VkTestSiteApp::createCommandPool() {
  ZoneScoped;
  const auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);

  const auto poolInfo = vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, indices.graphics);
  m_commandPool = m_device.createCommandPool(poolInfo);
}

void VkTestSiteApp::createCommandBuffers() {
  ZoneScoped;
  const auto commandBufInfo = vk::CommandBufferAllocateInfo(m_commandPool, vk::CommandBufferLevel::ePrimary,
                                                            m_swapchain.imageViews.size());
  m_commandBuffers = m_device.allocateCommandBuffers(commandBufInfo);
}

void VkTestSiteApp::createSyncObjects() {
  constexpr auto fenceInfo = vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled);
  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_inFlight.push_back(m_device.createFence(fenceInfo));
    m_imageAvailable.push_back(m_device.createSemaphore(vk::SemaphoreCreateInfo()));
    m_renderFinished.push_back(m_device.createSemaphore(vk::SemaphoreCreateInfo()));
  }
}

void VkTestSiteApp::mainLoop() {
  ZoneScoped;
  while (!glfwWindowShouldClose(m_window)) {
    glfwPollEvents();
    if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0) {
      ImGui_ImplGlfw_Sleep(10);
      continue;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Render();
    const auto draw_data = ImGui::GetDrawData();
    if (draw_data->DisplaySize.x > 0.0f && draw_data->DisplaySize.y > 0.0f) {
      render(draw_data);
      FrameMark;
    }
  }
}

void VkTestSiteApp::render(ImDrawData *draw_data) {
  ZoneScoped;
  auto _ = m_device.waitForFences(m_inFlight[m_currentFrame], true, UINT64_MAX);
  m_device.resetFences(m_inFlight[m_currentFrame]);

  uint32_t imageIndex;
  try {
    const auto acquireResult = m_device.acquireNextImageKHR(
      m_swapchain.swapchain, UINT64_MAX, m_imageAvailable[m_currentFrame], nullptr);
    imageIndex = acquireResult.value;
  } catch (vk::OutOfDateKHRError) {
    recreateSwapchain();
    return;
  } catch (vk::SystemError) {
    throw std::runtime_error("Failed to acquire swapchain image!");
  }

  recordCommandBuffer(draw_data, m_commandBuffers[imageIndex], imageIndex);

  vk::PipelineStageFlags pipelineStageFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  const auto submitInfo = vk::SubmitInfo(
    m_imageAvailable[m_currentFrame],
    pipelineStageFlags,
    m_commandBuffers[imageIndex],
    m_renderFinished[m_currentFrame]);
  m_graphicsQueue.submit(submitInfo, m_inFlight[m_currentFrame]);

  const auto presentInfo = vk::PresentInfoKHR(m_renderFinished[m_currentFrame], m_swapchain.swapchain, imageIndex);
  vk::Result presentResult;
  try {
    presentResult = m_presentQueue.presentKHR(presentInfo);
  } catch (vk::OutOfDateKHRError) {
    presentResult = vk::Result::eErrorOutOfDateKHR;
  } catch (vk::SystemError) {
    throw std::runtime_error("Failed to present swapchain image!");
  }

  if (presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR) {
    recreateSwapchain();
    return;
  }

  m_presentQueue.waitIdle();

  m_currentFrame = imageIndex;
}

void VkTestSiteApp::recordCommandBuffer(ImDrawData *draw_data, const vk::CommandBuffer &commandBuffer,
                                        uint32_t imageIndex) {
  ZoneScoped;
  commandBuffer.reset();
  commandBuffer.begin(vk::CommandBufferBeginInfo());

  const auto renderArea = vk::Rect2D({}, m_swapchain.extent);
  constexpr auto colorClearValue = vk::ClearValue(vk::ClearColorValue(0.53f, 0.81f, 0.92f, 1.0f));
  const auto beginInfo = vk::RenderPassBeginInfo(m_renderPass, m_framebuffers[imageIndex], renderArea, colorClearValue);

  commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
  const std::vector viewports = {
    vk::Viewport(0, 0, m_swapchain.extent.width, m_swapchain.extent.height, 0.0f, 1.0f)
  };
  commandBuffer.setViewport(0, viewports);
  const std::vector scissors = {
    vk::Rect2D({}, m_swapchain.extent),
  };
  commandBuffer.setScissor(0, scissors);
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);
  commandBuffer.draw(3, 1, 0, 0);
  ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
  commandBuffer.endRenderPass();
  commandBuffer.end();
}

void VkTestSiteApp::recreateSwapchain() {
  int width = 0, height = 0;
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(m_window, &width, &height);
    glfwWaitEvents();
  }

  m_device.waitIdle();
  cleanupSwapchain();

  m_swapchain = Swapchain(m_surface.get(), m_device, m_physicalDevice, m_window);
  createRenderPass();
  createPipeline();
  m_descriptorPool = DescriptorPool(m_device);
  //createDescriptorSet();
  createFramebuffers();
  createCommandBuffers();
}

void VkTestSiteApp::cleanupSwapchain() {
  //m_descriptorSet.destroy(m_device);
  m_descriptorPool.destroy(m_device);
  m_device.freeCommandBuffers(m_commandPool, m_commandBuffers);
  for (const auto framebuffer: m_framebuffers) {
    m_device.destroyFramebuffer(framebuffer);
  }
  m_device.destroyPipeline(m_graphicsPipeline);
  m_device.destroyPipelineLayout(m_pipelineLayout);
  m_device.destroyRenderPass(m_renderPass);
  m_swapchain.destroy(m_device);
}

void VkTestSiteApp::cleanup() {
  ZoneScoped;
  TracyVkDestroy(m_vkContext);

  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_device.destroyFence(m_inFlight[i]);
    m_device.destroySemaphore(m_imageAvailable[i]);
    m_device.destroySemaphore(m_renderFinished[i]);
  }

  cleanupSwapchain();

  m_device.destroyCommandPool(m_commandPool);
  vmaDestroyAllocator(m_allocator);
  m_device.destroy();
  m_instance.destroySurfaceKHR(m_surface.release());
#ifndef NDEBUG
  m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger, nullptr, m_didl);
#endif
  m_instance.destroy();
}
