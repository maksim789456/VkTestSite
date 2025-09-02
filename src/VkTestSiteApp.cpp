#include "VkTestSiteApp.h"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define MAX_FRAME_IN_FLIGHT 2 //0..2 -> 3 frames
#define MAX_MATERIAL_PER_DESCRIPTOR 64

const std::vector DEVICE_EXTENSIONS = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
  VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
  VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME
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
  m_loader = {};
  const auto vkGetInstanceProcAddr = m_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
  createInstance();

  VkSurfaceKHR surface_tmp;
  glfwCreateWindowSurface(m_instance, m_window, nullptr, &surface_tmp);
  m_surface = vk::UniqueSurfaceKHR(surface_tmp, m_instance);
  const auto deviceTmp = pickPhysicalDevice(m_instance, m_surface.get(), DEVICE_EXTENSIONS);
  if (!deviceTmp) {
    abort();
  }
  m_physicalDevice = *deviceTmp;
  const auto props = m_physicalDevice.getProperties();
  std::cout << "Physical device: " << props.deviceName << std::endl;
  m_msaaSamples = findMaxMsaaSamples(m_physicalDevice);
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
  createUniformBuffers();
  m_descriptorPool = DescriptorPool(m_device);
  createDescriptorSet();
  createPipeline();
  createCommandPool();
  createColorObjets();
  createDepthObjets();
  createFramebuffers();
  createCommandBuffers();
  createSyncObjects();
  m_texManager = std::make_unique<TextureManager>(
    m_device, m_graphicsQueue, m_commandPool, m_allocator, m_descriptorSet, 1);

  m_camera = std::make_unique<Camera>();
  auto keyCallback = [](GLFWwindow *window, int key, int scancode, int action, int mods) {
    const auto me = static_cast<VkTestSiteApp *>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureKeyboard)
      return;
    me->m_camera->keyboardCallback(key, action);
  };
  auto mouseCallback = [](GLFWwindow *window, double xpos, double ypos) {
    const auto me = static_cast<VkTestSiteApp *>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureMouse)
      return;
    me->m_camera->mouseCallback(window, xpos, ypos);
  };
  glfwSetWindowUserPointer(m_window, this);
  glfwSetKeyCallback(m_window, keyCallback);
  glfwSetCursorPosCallback(m_window, mouseCallback);

#ifndef NDEBUG
  const auto qpreset = reinterpret_cast<PFN_vkResetQueryPoolEXT>(vkGetDeviceProcAddr(m_device, "vkResetQueryPoolEXT"));
  const auto gpdctd = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(vkGetInstanceProcAddr(
    m_instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
  const auto gct = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(vkGetDeviceProcAddr(
    m_device, "vkGetCalibratedTimestampsEXT"));
  m_vkContext = tracy::CreateVkContext(m_physicalDevice, m_device, qpreset, gpdctd, gct);
#endif

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
  vkInitInfo.MSAASamples = static_cast<VkSampleCountFlagBits>(m_msaaSamples);
  vkInitInfo.Subpass = 0;
  vkInitInfo.DescriptorPoolSize = 100;
  vkInitInfo.CheckVkResultFn = [](const VkResult err) {
    if (err != VK_SUCCESS) std::cerr << "Imgui Vk Error: " << err << std::endl;
  };
  if (!ImGui_ImplVulkan_Init(&vkInitInfo)) {
    std::cerr << "Failed to initialize Imgui Vulkan render" << std::endl;
    abort();
  }

  const auto imguiCmdsInfo = vk::CommandBufferAllocateInfo(
    m_commandPool, vk::CommandBufferLevel::eSecondary, m_swapchain.imageViews.size()
  );
  m_imguiCommandBuffers = m_device.allocateCommandBuffersUnique(imguiCmdsInfo);
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

#ifndef NDEBUG
  required_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

  const auto enabled_extensions = gatherExtensions(required_extensions, vk::enumerateInstanceExtensionProperties());
  const auto enabled_layers = gatherLayers(required_layers, vk::enumerateInstanceLayerProperties());

  //vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR
  auto create_info = makeInstanceCreateInfoChain({}, app_info,
                                                 enabled_layers, enabled_extensions);
  m_instance = vk::createInstance(create_info.get<vk::InstanceCreateInfo>());
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

#ifndef NDEBUG
  m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(create_info.get<vk::DebugUtilsMessengerCreateInfoEXT>());
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
  vk::PhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features{};
  descriptor_indexing_features
      .setDescriptorBindingPartiallyBound(true)
      .setDescriptorBindingSampledImageUpdateAfterBind(true)
      .setShaderSampledImageArrayNonUniformIndexing(true)
      .setRuntimeDescriptorArray(true)
      .setDescriptorBindingVariableDescriptorCount(true);
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
  device_create_info.setPNext(&descriptor_indexing_features);

  m_device = m_physicalDevice.createDevice(device_create_info);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);
}

void VkTestSiteApp::createRenderPass() {
  ZoneScoped;
  const auto colorAttachment = vk::AttachmentDescription(
    {}, m_swapchain.format, m_msaaSamples,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
  constexpr auto colorAttachmentRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);

  const auto depthStencilAttachment = vk::AttachmentDescription(
    {}, vk::Format::eD32SfloatS8Uint, m_msaaSamples,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
  constexpr auto depthStencilAttachmentRef =
      vk::AttachmentReference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

  const auto colorResolveAttachment = vk::AttachmentDescription(
    {}, m_swapchain.format, vk::SampleCountFlagBits::e1,
    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
  constexpr auto colorResolveAttachmentRef = vk::AttachmentReference(2, vk::ImageLayout::eColorAttachmentOptimal);

  std::vector colorAttachmentsRefs = {colorAttachmentRef};
  std::vector resolveAttachmentsRefs = {colorResolveAttachmentRef};
  auto subpass = vk::SubpassDescription(
    {}, vk::PipelineBindPoint::eGraphics,
    {},
    colorAttachmentsRefs,
    resolveAttachmentsRefs,
    &depthStencilAttachmentRef
  );

  auto dependency = vk::SubpassDependency(
    vk::SubpassExternal, 0,
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
    {},
    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite
  );

  std::vector attachments = {colorAttachment, depthStencilAttachment, colorResolveAttachment};
  std::vector subpasses = {subpass};
  const auto renderPassInfo = vk::RenderPassCreateInfo({}, attachments, subpass, dependency);

  m_renderPass = m_device.createRenderPass(renderPassInfo);
}

void VkTestSiteApp::createPipeline() {
  ZoneScoped;
  auto shaderModule = ShaderModule();
  shaderModule.load(m_device, "../res/shaders/test.slang.spv");
  shaderModule.reflect(m_device);
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
  auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo({}, bindingDescription, attributeDescription);
  auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList, false);
  auto viewportState = vk::PipelineViewportStateCreateInfo({}, 1, nullptr, 1, nullptr);
  auto rasterizer = vk::PipelineRasterizationStateCreateInfo(
    {}, false, false, vk::PolygonMode::eFill,
    vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise,
    false, {}, {}, {}, 1.0f);
  auto multisampling = vk::PipelineMultisampleStateCreateInfo({}, m_msaaSamples, true, 0.2f);
  auto depthStencil = vk::PipelineDepthStencilStateCreateInfo({}, true, true, vk::CompareOp::eGreaterOrEqual);
  depthStencil.setDepthBoundsTestEnable(false)
      .setMinDepthBounds(0.0f)
      .setMaxDepthBounds(1.0f);
  auto colorAttachment = vk::PipelineColorBlendAttachmentState(
    true,
    vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha,
    vk::BlendOp::eAdd,
    vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha,
    vk::BlendOp::eAdd);
  colorAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  std::vector colorAttachments = {colorAttachment};
  auto colorBlend = vk::PipelineColorBlendStateCreateInfo({}, false, vk::LogicOp::eCopy, colorAttachments);

  auto pipelineInfo = vk::GraphicsPipelineCreateInfo({});
  pipelineInfo.setStages(shaderStages)
      .setPVertexInputState(&vertexInputInfo)
      .setPInputAssemblyState(&inputAssembly)
      .setPViewportState(&viewportState)
      .setPRasterizationState(&rasterizer)
      .setPMultisampleState(&multisampling)
      .setPDepthStencilState(&depthStencil)
      .setPColorBlendState(&colorBlend)
      .setPDynamicState(&dynamicStateInfo)
      .setLayout(m_descriptorSet.getPipelineLayout())
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
}

void VkTestSiteApp::createColorObjets() {
  m_color = std::make_unique<Texture>(
    m_device, m_allocator,
    m_swapchain.extent.width, m_swapchain.extent.height, 1,
    m_swapchain.format,
    m_msaaSamples,
    vk::ImageAspectFlagBits::eColor,
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
    false, "Color attachment"
  );
}

void VkTestSiteApp::createDepthObjets() {
  constexpr auto depthFormat = vk::Format::eD32SfloatS8Uint;

  m_depth = std::make_unique<Texture>(
    m_device, m_allocator,
    m_swapchain.extent.width, m_swapchain.extent.height, 1,
    depthFormat,
    m_msaaSamples,
    vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
    vk::ImageUsageFlagBits::eDepthStencilAttachment,
    false, "Depth attachment"
  );

  transitionImageLayout(
    m_device, m_graphicsQueue, m_commandPool,
    m_depth->getImage(),
    depthFormat,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eDepthStencilAttachmentOptimal, 1
  );
}

void VkTestSiteApp::createFramebuffers() {
  ZoneScoped;
  m_framebuffers.resize(m_swapchain.imageViews.size());
  for (size_t i = 0; i < m_swapchain.imageViews.size(); ++i) {
    std::vector attachments = {m_color->getImageView(), m_depth->getImageView(), m_swapchain.imageViews[i]};

    auto framebufferInfo = vk::FramebufferCreateInfo(
      {}, m_renderPass, attachments,
      m_swapchain.extent.width, m_swapchain.extent.height, 1
    );
    m_framebuffers[i] = m_device.createFramebuffer(framebufferInfo);
  }
}

void VkTestSiteApp::createUniformBuffers() {
  ZoneScoped;
  for (size_t i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_uniforms.emplace_back(m_allocator,
                            vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
  }
}

void VkTestSiteApp::createDescriptorSet() {
  ZoneScoped;
  std::vector<vk::DescriptorBufferInfo> uniform_infos;
  for (const auto &ub: m_uniforms) {
    uniform_infos.emplace_back(ub.getBufferInfo());
  }

  const auto layouts = std::vector{
    DescriptorLayout{
      .type = vk::DescriptorType::eUniformBuffer,
      .stage = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
      .bindingFlags = {},
      .shaderBinding = 0,
      .count = 1,
      .imageInfos = {},
      .bufferInfos = uniform_infos
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
  const auto pushConsts = std::vector{
    vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(ModelPushConsts))
  };
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
    float currentTime = static_cast<float>(glfwGetTime());
    float deltaTime = currentTime - m_lastTime;
    m_lastTime = currentTime;
    glfwPollEvents();
    if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0) {
      ImGui_ImplGlfw_Sleep(10);
      continue;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();
    ImGui::Begin("Test menu");
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Once);
    if (!m_modelLoaded && ImGui::Button("Load model")) {
      auto path = tinyfd_openFileDialog("Open model file", nullptr, 0, nullptr, nullptr, 0);
      if (path != nullptr) {
        auto pathStr = std::string(path);
        m_model = std::make_unique<Model>(m_device, m_graphicsQueue, m_commandPool, m_allocator, *m_texManager,
                                          pathStr);
        m_model->createCommandBuffers(m_device, m_commandPool, m_swapchain.imageViews.size());
        m_modelLoaded = true;
      }
    }
    if (m_modelLoaded && ImGui::Button("Unload model")) {
      m_model.reset();
      m_modelLoaded = false;
    }
    ImGui::End();

    if (m_modelLoaded && ImGui::Begin("Texture Browser")) {
      static int selected = -1; {
        ImGui::BeginChild("Slots", ImVec2(ImGui::GetContentRegionAvail().x * 0.2f, 260), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto &id: m_texManager->m_textures | std::views::keys) {
          if (ImGui::Selectable(std::format("Slot: {}", id).c_str(), selected == id)) {
            selected = id;
          }
        }
        ImGui::EndChild();
      }
      ImGui::SameLine();

      ImGui::BeginChild("Preview", ImVec2(0, 260));
      if (selected != -1) {
        if (auto tex = m_texManager->getTexture(selected); tex.has_value()) {
          float scale = 1.0f;
          if (tex.value()->width > tex.value()->height) {
            scale = 256.0f / tex.value()->width;
          } else {
            scale = 256.0f / tex.value()->height;
          }

          ImVec2 previewSize(tex.value()->width * scale, tex.value()->height * scale);
          ImGui::Image(tex.value()->getImGuiID(), previewSize);
        }
      } else {
        ImGui::Text("Select a slot...");
      }
      ImGui::EndChild();
      ImGui::End();
    }

    ImGui::Render();
    const auto draw_data = ImGui::GetDrawData();
    if (draw_data->DisplaySize.x > 0.0f && draw_data->DisplaySize.y > 0.0f) {
      render(draw_data, deltaTime);
      FrameMark;
    }
  }
}

void VkTestSiteApp::render(ImDrawData *draw_data, float deltaTime) {
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

  m_camera->onUpdate(deltaTime);
  updateUniformBuffer(imageIndex);
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

void VkTestSiteApp::updateUniformBuffer(uint32_t imageIndex) {
  auto ubo = UniformBufferObject{
    glm::vec4(m_camera->getViewPos(), 1.0f),
    m_camera->getViewProj(),
    m_camera->getInvViewProj()
  };
  m_uniforms[imageIndex].map(ubo);
}

void VkTestSiteApp::recordCommandBuffer(ImDrawData *draw_data, const vk::CommandBuffer &commandBuffer,
                                        uint32_t imageIndex) {
  ZoneScoped;
  commandBuffer.reset();
  commandBuffer.begin(vk::CommandBufferBeginInfo());

  const auto renderArea = vk::Rect2D({}, m_swapchain.extent);
  auto colorClearValue = m_modelLoaded
                           ? vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f))
                           : vk::ClearValue(vk::ClearColorValue(0.53f, 0.81f, 0.92f, 1.0f));
  auto depthClearValue = vk::ClearValue(vk::ClearDepthStencilValue(0.0f, 0));
  auto clearValues = {colorClearValue, depthClearValue};
  const auto beginInfo = vk::RenderPassBeginInfo(m_renderPass, m_framebuffers[imageIndex], renderArea, clearValues);

  commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eSecondaryCommandBuffers); {
    // Model temp render
    if (m_modelLoaded) {
      auto modelCmd = m_model->cmdDraw(
        m_framebuffers[imageIndex],
        m_renderPass,
        m_graphicsPipeline,
        m_swapchain,
        m_descriptorSet,
        0,
        imageIndex
      );

      commandBuffer.executeCommands(modelCmd);
    }
  } {
    // ImGUI Secondary Cmd record -> exec
    auto imguiCmd = m_imguiCommandBuffers[imageIndex].get();
    auto inheritanceInfo = vk::CommandBufferInheritanceInfo(m_renderPass, 0, m_framebuffers[imageIndex]);
    auto imguiBeginInfo = vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eRenderPassContinue | vk::CommandBufferUsageFlagBits::eSimultaneousUse,
      &inheritanceInfo);
    imguiCmd.reset();
    imguiCmd.begin(imguiBeginInfo);
    ImGui_ImplVulkan_RenderDrawData(draw_data, imguiCmd);
    imguiCmd.end();

    commandBuffer.executeCommands(imguiCmd);
  }

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
  createUniformBuffers();
  m_descriptorPool = DescriptorPool(m_device);
  createDescriptorSet();
  createPipeline();
  createColorObjets();
  createDepthObjets();
  createFramebuffers();
  createCommandBuffers();
}

void VkTestSiteApp::cleanupSwapchain() {
  for (auto &uniform: m_uniforms) {
    uniform.destroy();
  }
  m_uniforms.clear();
  m_descriptorSet.destroy(m_device);
  m_descriptorPool.destroy(m_device);
  m_device.freeCommandBuffers(m_commandPool, m_commandBuffers);
  m_color.reset();
  m_depth.reset();
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
#ifndef NDEBUG
  TracyVkDestroy(m_vkContext);
#endif

  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_device.destroyFence(m_inFlight[i]);
    m_device.destroySemaphore(m_imageAvailable[i]);
    m_device.destroySemaphore(m_renderFinished[i]);
  }

  cleanupSwapchain();

  if (m_modelLoaded)
    m_model.reset();
  m_texManager.reset();
  m_imguiCommandBuffers.clear();
  m_device.destroyCommandPool(m_commandPool);
  vmaDestroyAllocator(m_allocator);
  m_device.destroy();
  m_instance.destroySurfaceKHR(m_surface.release());
#ifndef NDEBUG
  m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
#endif
  m_instance.destroy();
}
