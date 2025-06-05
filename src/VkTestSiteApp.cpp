#include "VkTestSiteApp.h"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define MAX_FRAME_IN_FLIGHT 2 //0..2 -> 3 frames

const std::vector DEVICE_EXTENSIONS = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

const std::vector LAYERS = {
#ifndef NDEBUG
  "VK_LAYER_KHRONOS_validation"
#endif
};

void VkTestSiteApp::run() {
  ZoneScoped;
  initWindow();
  initVk();
  mainLoop();
  cleanup();
}

void VkTestSiteApp::initWindow() {
  ZoneScoped;
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

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
  m_swapchain = Swapchain(m_surface.get(), m_device, m_physicalDevice, m_window);
  createRenderPass();
  createPipeline();
  createFramebuffers();
  createCommandPool();
  createCommandBuffers();
  createSyncObjects();
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
  auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);
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

  m_device = m_physicalDevice.createDevice(device_create_info);
}

void VkTestSiteApp::createRenderPass() {
  ZoneScoped;
  auto colorAttachment = vk::AttachmentDescription(
    {}, m_swapchain.format, vk::SampleCountFlagBits::e1,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
  auto colorAttachmentRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);

  std::vector<vk::AttachmentReference> inputAttachmentsRefs = {};
  std::vector colorAttachmentsRefs = {colorAttachmentRef};
  auto subpass = vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, inputAttachmentsRefs,
                                        colorAttachmentsRefs);

  std::vector attachments = {colorAttachment};
  std::vector subpasses = {subpass};
  auto renderPassInfo = vk::RenderPassCreateInfo({}, attachments, subpass);

  m_renderPass = m_device.createRenderPass(renderPassInfo);
}

void VkTestSiteApp::createPipeline() {
  ZoneScoped;
  auto vertexShaderModule = ShaderModule(vk::ShaderStageFlagBits::eVertex);
  vertexShaderModule.load("../res/shaders/test.vert");
  vertexShaderModule.compile(m_device);
  auto fragmentShaderModule = ShaderModule(vk::ShaderStageFlagBits::eFragment);
  fragmentShaderModule.load("../res/shaders/test.frag");
  fragmentShaderModule.compile(m_device);
  std::vector shaderStages = {
    vertexShaderModule.pipeline_info, fragmentShaderModule.pipeline_info
  };

  std::vector dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
  };

  auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo({}, dynamicStates);
  auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo({}, 0, nullptr, 0, nullptr); //TODO
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

  vertexShaderModule.destroy(m_device);
  fragmentShaderModule.destroy(m_device);
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

void VkTestSiteApp::createCommandPool() {
  ZoneScoped;
  auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);

  auto poolInfo = vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, indices.graphics);
  m_commandPool = m_device.createCommandPool(poolInfo);
}

void VkTestSiteApp::createCommandBuffers() {
  ZoneScoped;
  auto commandBufInfo = vk::CommandBufferAllocateInfo(m_commandPool, vk::CommandBufferLevel::ePrimary,
                                                      m_swapchain.imageViews.size());
  m_commandBuffers = m_device.allocateCommandBuffers(commandBufInfo);
}

void VkTestSiteApp::createSyncObjects() {
  auto fenceInfo = vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled);
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
    render();
    FrameMark;
  }
}

void VkTestSiteApp::render() {
  ZoneScoped;
  auto _ = m_device.waitForFences(m_inFlight[m_currentFrame], true, UINT64_MAX);
  m_device.resetFences(m_inFlight[m_currentFrame]);

  auto acquireResult = m_device.acquireNextImageKHR(
    m_swapchain.swapchain, UINT64_MAX, m_imageAvailable[m_currentFrame], nullptr);

  if (acquireResult.result != vk::Result::eSuccess) {
    std::cerr << "Swapchain is dirty" << std::endl;
    abort();
  }
  uint32_t imageIndex = acquireResult.value;

  recordCommandBuffer(m_commandBuffers[imageIndex], imageIndex);

  vk::PipelineStageFlags pipelineStageFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  auto submitInfo = vk::SubmitInfo(
    m_imageAvailable[m_currentFrame],
    pipelineStageFlags,
    m_commandBuffers[imageIndex],
    m_renderFinished[m_currentFrame]);
  m_graphicsQueue.submit(submitInfo, m_inFlight[m_currentFrame]);

  auto presentInfo = vk::PresentInfoKHR(m_renderFinished[m_currentFrame], m_swapchain.swapchain, imageIndex);
  auto presentResult = m_presentQueue.presentKHR(presentInfo);
  if (presentResult != vk::Result::eSuccess) {
    std::cerr << "Swapchain is dirty" << std::endl;
    abort();
  }
  m_presentQueue.waitIdle();

  m_currentFrame = imageIndex;
}

void VkTestSiteApp::recordCommandBuffer(const vk::CommandBuffer &commandBuffer, uint32_t imageIndex) {
  ZoneScoped;
  commandBuffer.reset();
  commandBuffer.begin(vk::CommandBufferBeginInfo());

  auto renderArea = vk::Rect2D({}, m_swapchain.extent);
  auto colorClearValue = vk::ClearValue(vk::ClearColorValue(0.53f, 0.81f, 0.92f, 1.0f));
  auto beginInfo = vk::RenderPassBeginInfo(m_renderPass, m_framebuffers[imageIndex], renderArea, colorClearValue);

  commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
  std::vector viewports = {
    vk::Viewport(0, 0, m_swapchain.extent.width, m_swapchain.extent.height, 0.0f, 1.0f)
  };
  commandBuffer.setViewport(0, viewports);
  std::vector scissors = {
    vk::Rect2D({}, m_swapchain.extent),
  };
  commandBuffer.setScissor(0, scissors);
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);
  commandBuffer.draw(3, 1, 0, 0);
  commandBuffer.endRenderPass();
  commandBuffer.end();
}

void VkTestSiteApp::cleanup() {
  ZoneScoped;
  m_device.waitIdle();

  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_device.destroyFence(m_inFlight[i]);
    m_device.destroySemaphore(m_imageAvailable[i]);
    m_device.destroySemaphore(m_renderFinished[i]);
  }

  m_device.freeCommandBuffers(m_commandPool, m_commandBuffers);
  m_device.destroyCommandPool(m_commandPool);
  for (auto framebuffer: m_framebuffers) {
    m_device.destroyFramebuffer(framebuffer);
  }
  m_device.destroyPipeline(m_graphicsPipeline);
  m_device.destroyPipelineLayout(m_pipelineLayout);
  m_device.destroyRenderPass(m_renderPass);

  m_swapchain.destroy(m_device);
  m_device.destroy();
#ifndef NDEBUG
  m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger, nullptr, m_didl);
#endif
  m_instance.destroySurfaceKHR(m_surface.release());
  m_instance.destroy();

  glfwDestroyWindow(m_window);
  glfwTerminate();
}
