#include "VkTestSiteApp.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
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
  VmaAllocator vmaAllocator;
  auto allocCreateResult = vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
  if (allocCreateResult != VK_SUCCESS) {
    std::cerr << "vmaCreateAllocator failed with error code: " << allocCreateResult << std::endl;
    throw std::runtime_error("Failed to create VMA allocator");
  }
  m_allocator = vma::Allocator(vmaAllocator);

  m_swapchain = Swapchain(m_surface.get(), m_device, m_physicalDevice, m_window);
  createRenderPass();
  createUniformBuffers();
  m_descriptorPool = DescriptorPool(m_device);
  m_lightManager = std::make_unique<LightManager>(
    m_allocator, m_swapchain.imageViews.size());
  createCommandPool();
  createColorObjets();
  createDepthObjets();
  createDescriptorSet();
  createPipeline();
  const auto lightCmdsInfo = vk::CommandBufferAllocateInfo(
    m_commandPool, vk::CommandBufferLevel::eSecondary, m_swapchain.imageViews.size()
  );
  m_lightingCommandBuffers = m_device.allocateCommandBuffersUnique(lightCmdsInfo);
  createFramebuffers();
  createCommandBuffers();
  createSyncObjects();
  const auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);
  m_stagingBuffer = std::make_unique<StagingBuffer>(m_device, m_allocator, 128 * 1024 * 1024); // 64 MB
  m_transferThread = std::make_unique<TransferThread>(m_device, m_transferQueue, indices.transfer, *m_stagingBuffer);
  m_textureWorkerPool = std::make_unique<TextureWorkerPool>(m_device, m_allocator, *m_stagingBuffer, *m_transferThread);
  m_texManager = std::make_unique<TextureManager>(
    m_device, m_graphicsQueue, m_commandPool, *m_textureWorkerPool, m_cfrDescriptorSet, 2);

  m_camera = std::make_unique<Camera>(m_swapchain.extent);
  auto keyCallback = [](GLFWwindow *window, int key, int scancode, int action, int mods) {
    const auto me = static_cast<VkTestSiteApp *>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureKeyboard)
      return;
    me->m_camera->keyboardCallback(key, action, mods);
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
  const auto gpdctd = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(vkGetInstanceProcAddr(
    m_instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
  const auto gct = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(vkGetDeviceProcAddr(
    m_device, "vkGetCalibratedTimestampsEXT"));
  m_tracyCmdBuffer = m_device.allocateCommandBuffers(
    vk::CommandBufferAllocateInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, 1))[0];
  m_vkContext = tracy::CreateVkContext(m_physicalDevice, m_device, m_graphicsQueue, m_tracyCmdBuffer, gpdctd, gct);
  const std::string contextName = "Graphics Queue";
  m_vkContext->Name(contextName.data(), contextName.size());
#endif

  ImGui_ImplGlfw_InitForVulkan(m_window, true);
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

  const auto enabled_extensions = gatherExtensions(required_extensions
#ifndef NDEBUG
                                                   , vk::enumerateInstanceExtensionProperties()
#endif
  );
  const auto enabled_layers = gatherLayers(required_layers
#ifndef NDEBUG
                                           , vk::enumerateInstanceLayerProperties()
#endif
  );

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
  m_transferQueue = m_device.getQueue(indices.transfer, 1);
}

void VkTestSiteApp::createLogicalDevice() {
  ZoneScoped;
  auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);

  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
  std::vector<std::vector<float> > queuePrioritiesStorage;
  std::set queue_families = {indices.graphics, indices.present, indices.transfer};

  for (uint32_t queue_family: queue_families) {
    uint32_t count = 1;
    if (queue_family == indices.graphics && indices.graphics == indices.transfer) {
      count = 2;
    }

    queuePrioritiesStorage.emplace_back(count, 1.0f);
    auto &priorities = queuePrioritiesStorage.back();

    vk::DeviceQueueCreateInfo queue_create_info{};
    queue_create_info
        .setQueueFamilyIndex(queue_family)
        .setQueueCount(count)
        .setPQueuePriorities(priorities.data());
    queue_create_infos.push_back(queue_create_info);
  }

  vk::PhysicalDeviceFeatures device_features{};
  vk::PhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features{};
  vk::PhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{};
  vk::PhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features{};
  vk::PhysicalDeviceVulkan13Features features13{};

  hostQueryResetFeatures.setHostQueryReset(true);
  timeline_semaphore_features
      .setTimelineSemaphore(true)
      .setPNext(&hostQueryResetFeatures);
  descriptor_indexing_features
      .setDescriptorBindingPartiallyBound(true)
      .setDescriptorBindingSampledImageUpdateAfterBind(true)
      .setShaderSampledImageArrayNonUniformIndexing(true)
      .setRuntimeDescriptorArray(true)
      .setDescriptorBindingVariableDescriptorCount(true)
      .setPNext(&timeline_semaphore_features);

  features13
      .setSynchronization2(true)
      .setPNext(&descriptor_indexing_features);

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
  device_create_info.setPNext(&features13);

  m_device = m_physicalDevice.createDevice(device_create_info);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);
}

void VkTestSiteApp::createRenderPass() {
  ZoneScoped;
  const auto attachments = {
    vk::AttachmentDescription( // Depth
      {}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilReadOnlyOptimal),
    vk::AttachmentDescription( // Final color (swapchain)
      {}, m_swapchain.format, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR)
  };

  constexpr auto depthRef = vk::AttachmentReference{0, vk::ImageLayout::eDepthStencilAttachmentOptimal};
  const vk::AttachmentReference colorRef{1, vk::ImageLayout::eColorAttachmentOptimal};
  const auto color = {colorRef};
  auto subpass0 = vk::SubpassDescription(
    {}, vk::PipelineBindPoint::eGraphics,
    {}, color, {}, &depthRef
  );

  auto dependencies = {
    vk::SubpassDependency(
      vk::SubpassExternal, 0,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
      {},
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite),
  };

  std::vector subpasses = {subpass0};
  const auto renderPassInfo = vk::RenderPassCreateInfo({}, attachments, subpasses, dependencies);

  m_renderPass = m_device.createRenderPass(renderPassInfo);
}

void VkTestSiteApp::createPipeline() {
  ZoneScoped;
  m_preDepthPipeline = PipelineBuilder(
        m_device,
        m_renderPass,
        m_geometryDescriptorSet.getPipelineLayout(),
        "../res/shaders/clustered_forward/pre_depth.ep.slang.spv",
        "Depth Pre-Pass Pipeline"
      )
      .withBindingDescriptions({Vertex::GetBindingDescription()})
      .withAttributeDescriptions({Vertex::GetAttributeDescriptions()})
      .withColorBlendAttachments({
        PipelineBuilder::makeDefaultColorAttachmentState(),
      })
      .depthStencil(true, true, vk::CompareOp::eGreaterOrEqual)
      .withSubpass(0)
      .buildGraphics();
  m_clusterComputePipeline = PipelineBuilder(
        m_device,
        m_renderPass,
        m_clusterComputeDescriptorSet.getPipelineLayout(),
        "../res/shaders/clustered_forward/clusters.cmp.slang.spv",
        "Cluster Compute Pipeline"
      )
      .buildCompute();
}

void VkTestSiteApp::createColorObjets() {
  m_clusterCount = XSLICES * YSLICES * ZSLICES;
  m_clustersCountBufferSize = m_clusterCount * sizeof(uint32_t);
  m_clustersIndicesBufferSize = m_clusterCount * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t);

  m_clustersCount = createBufferUnique(
    m_allocator, m_clustersCountBufferSize,
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc,
    vma::MemoryUsage::eAutoPreferDevice);
  m_clustersIndices = createBufferUnique(
    m_allocator, m_clustersIndicesBufferSize,
    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vma::MemoryUsage::eAutoPreferDevice);
}

void VkTestSiteApp::createDepthObjets() {
  constexpr auto depthFormat = vk::Format::eD32Sfloat;

  m_depth = std::make_unique<Texture>(
    m_device, m_allocator,
    m_swapchain.extent.width, m_swapchain.extent.height, 1,
    depthFormat,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eDepth,
    vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment |
    vk::ImageUsageFlagBits::eSampled,
    true, "Depth attachment"
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
    std::vector attachments = {
      m_depth->getImageView(),
      m_swapchain.imageViews[i]
    };

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
    m_cameraMultiple.emplace_back(m_allocator,
                                  vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
  }
}

void VkTestSiteApp::createDescriptorSet() {
  ZoneScoped;
  std::vector<vk::DescriptorBufferInfo> uniform_infos;
  for (const auto &ub: m_uniforms) {
    uniform_infos.emplace_back(ub.getBufferInfo());
  }
  const auto uboDescriptor = DescriptorLayout{
    .type = vk::DescriptorType::eUniformBuffer,
    .stage = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    .bindingFlags = {},
    .shaderBinding = 0,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = uniform_infos
  };

  std::vector<vk::DescriptorBufferInfo> uniform_infos2;
  for (const auto &ub: m_cameraMultiple) {
    uniform_infos2.emplace_back(ub.getBufferInfo());
  }
  const auto ubo2Descriptor = DescriptorLayout{
    .type = vk::DescriptorType::eUniformBuffer,
    .stage = vk::ShaderStageFlagBits::eCompute,
    .bindingFlags = {},
    .shaderBinding = 0,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = uniform_infos2
  };

  const auto lightsDescriptor = DescriptorLayout{
    .type = vk::DescriptorType::eStorageBuffer,
    .stage = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
    .bindingFlags = {},
    .shaderBinding = 1,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = m_lightManager->getBufferInfos()
  };

  const auto clusterCountDescriptor = DescriptorLayout{
    .type = vk::DescriptorType::eStorageBuffer,
    .stage = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
    .bindingFlags = {},
    .shaderBinding = 3,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = {
      vk::DescriptorBufferInfo(m_clustersCount.first.get(), 0, m_clustersCountBufferSize),
      vk::DescriptorBufferInfo(m_clustersCount.first.get(), 0, m_clustersCountBufferSize),
      vk::DescriptorBufferInfo(m_clustersCount.first.get(), 0, m_clustersCountBufferSize),
    }
  };
  const auto clusterIndicesDescriptor = DescriptorLayout{
    .type = vk::DescriptorType::eStorageBuffer,
    .stage = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
    .bindingFlags = {},
    .shaderBinding = 4,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = {
      vk::DescriptorBufferInfo(m_clustersIndices.first.get(), 0, m_clustersIndicesBufferSize),
      vk::DescriptorBufferInfo(m_clustersIndices.first.get(), 0, m_clustersIndicesBufferSize),
      vk::DescriptorBufferInfo(m_clustersIndices.first.get(), 0, m_clustersIndicesBufferSize),
    }
  };

  m_geometryDescriptorSet = DescriptorSet(
    m_device, m_descriptorPool.getDescriptorPool(), m_swapchain.imageViews.size(),
    {
      uboDescriptor
    }, {
      vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(ModelPushConsts))
    });
  m_clusterComputeDescriptorSet = DescriptorSet(
    m_device, m_descriptorPool.getDescriptorPool(), m_swapchain.imageViews.size(),
    {
      ubo2Descriptor,
      lightsDescriptor,
      DescriptorLayout{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .stage = vk::ShaderStageFlagBits::eCompute,
        .bindingFlags = {},
        .shaderBinding = 2,
        .count = 1,
        .imageInfos = {
          vk::DescriptorImageInfo(m_depth->getSampler(), m_depth->getImageView(),
                                  vk::ImageLayout::eShaderReadOnlyOptimal)
        },
        .bufferInfos = {}
      },
      clusterCountDescriptor,
      clusterIndicesDescriptor,
    }, {
      vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(LightPushConsts))
    });
  m_cfrDescriptorSet = DescriptorSet(
    m_device, m_descriptorPool.getDescriptorPool(), m_swapchain.imageViews.size(),
    {
      uboDescriptor,
      lightsDescriptor,
      DescriptorLayout{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind,
        .shaderBinding = 2,
        .count = MAX_TEXTURE_PER_DESCRIPTOR,
        .imageInfos = {},
        .bufferInfos = {}
      },
      clusterCountDescriptor,
      clusterIndicesDescriptor
    }, {
      vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(ModelPushConsts))
    });
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
    const auto currentTime = static_cast<float>(glfwGetTime());
    const float deltaTime = currentTime - m_lastTime;
    m_lastTime = currentTime;
    glfwPollEvents();
    m_texManager->checkTextureLoading();
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
    const auto cameraPos = m_camera->getViewPos();
    ImGui::Text("Camera pos: %f %f %f", cameraPos.x, cameraPos.y, cameraPos.z);
    if (!m_modelLoaded && ImGui::Button("Load model")) {
      ZoneScopedN("Model loading");
      auto path = tinyfd_openFileDialog("Open model file", nullptr, 0, nullptr, nullptr, 0);
      if (path != nullptr) {
        auto pathStr = std::string(path);
        m_model = std::make_unique<Model>(
          m_device, m_graphicsQueue, m_commandPool, m_allocator, *m_texManager, pathStr);
        m_model->createCommandBuffers(m_device, m_commandPool, m_swapchain.imageViews.size());
        m_modelLoaded = true;
      }
    }
    if (m_modelLoaded && ImGui::Button("Unload model")) {
      m_model.reset();
      m_modelLoaded = false;
    }

    if (m_modelLoaded && ImGui::Button("Dump VMA stats")) {
      char *statsString = nullptr;
      vmaBuildStatsString(m_allocator, &statsString, true); {
        std::ofstream outStats{"VmaStats.json"};
        outStats << statsString;
        spdlog::info("VMA stats json saved at VmaStats.json file");
      }
      vmaFreeStatsString(m_allocator, statsString);
    }

    ImGui::Separator();
    ImGui::Text("Select G-Buffer Debug Output");
    ImGui::RadioButton("None", &m_debugView, 0);
    ImGui::RadioButton("Depth", &m_debugView, 1);
    ImGui::RadioButton("Albedo", &m_debugView, 2);
    ImGui::RadioButton("Normal", &m_debugView, 3);
    ImGui::RadioButton("Normal (TBN)", &m_debugView, 4);
    ImGui::RadioButton("Tangent (TBN)", &m_debugView, 5);
    ImGui::RadioButton("BiTangent (TBN)", &m_debugView, 6);
    ImGui::End();

    if (m_modelLoaded && ImGui::Begin("Texture Browser")) {
      static unsigned int selected = -1; {
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
          const auto width = static_cast<float>(tex.value()->width);
          const auto height = static_cast<float>(tex.value()->height);
          if (width > height) {
            scale = 256.0f / width;
          } else {
            scale = 256.0f / height;
          }

          ImVec2 previewSize(width * scale, height * scale);
          ImGui::Image(tex.value()->getImGuiID(), previewSize);
        }
      } else {
        ImGui::Text("Select a slot...");
      }
      ImGui::EndChild();
      ImGui::End();
    }

    m_lightManager->renderImGui();
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
  } catch (vk::OutOfDateKHRError &) {
    recreateSwapchain();
    return;
  } catch (vk::SystemError &) {
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

  executeSingleTimeCommands(m_device, m_graphicsQueue, m_commandPool, [&](const vk::CommandBuffer cmd) {
    m_vkContext->Collect(cmd);
  });

  const auto presentInfo = vk::PresentInfoKHR(m_renderFinished[m_currentFrame], m_swapchain.swapchain, imageIndex);
  vk::Result presentResult;
  try {
    presentResult = m_presentQueue.presentKHR(presentInfo);
  } catch (vk::OutOfDateKHRError &) {
    presentResult = vk::Result::eErrorOutOfDateKHR;
  } catch (vk::SystemError &) {
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
  auto projInfo = glm::vec4(m_swapchain.extent.width, m_swapchain.extent.height, m_camera->getZNear(),
                            m_camera->getZFar());
  auto ubo = UniformBufferObject{
    glm::vec4(m_camera->getViewPos(), 1.0f),
    m_camera->getViewProj(),
    m_camera->getInvViewProj(),
    projInfo,
    static_cast<uint32_t>(m_debugView)
  };
  auto cameraData = CameraData{
    m_camera->getView(),
    m_camera->getViewProj(),
    projInfo
  };
  m_uniforms[imageIndex].map(ubo);
  m_cameraMultiple[imageIndex].map(cameraData);
  m_lightManager->map(imageIndex);
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
  auto clearValues = {depthClearValue, colorClearValue};
  const auto beginInfo = vk::RenderPassBeginInfo(m_renderPass, m_framebuffers[imageIndex], renderArea, clearValues);

  commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eSecondaryCommandBuffers); {
    // Depth pre-pass
    if (m_modelLoaded) {
      auto modelCmd = m_model->cmdDraw(
        *m_vkContext,
        m_framebuffers[imageIndex],
        m_renderPass,
        m_preDepthPipeline,
        m_swapchain,
        m_geometryDescriptorSet,
        0,
        imageIndex
      );

      commandBuffer.executeCommands(modelCmd);
    }
  }
  commandBuffer.nextSubpass(vk::SubpassContents::eSecondaryCommandBuffers); {
    //Light subpass
    auto lightCmd = m_lightingCommandBuffers[imageIndex].get();
    auto inheritanceInfo = vk::CommandBufferInheritanceInfo(m_renderPass, 1, m_framebuffers[imageIndex]);
    auto lightBeginInfo = vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eRenderPassContinue | vk::CommandBufferUsageFlagBits::eSimultaneousUse,
      &inheritanceInfo);
    lightCmd.reset();
    lightCmd.begin(lightBeginInfo); {
      TracyVkZone(m_vkContext, lightCmd, "Light Pass");
      m_swapchain.cmdSetViewport(lightCmd);
      m_swapchain.cmdSetScissor(lightCmd);
      lightCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_lightingPipeline);
      m_lightingDescriptorSet.bind(lightCmd, imageIndex, {});
      auto lightPush = LightPushConsts{.lightCount = m_lightManager->getCount()};
      lightCmd.pushConstants(m_lightingDescriptorSet.getPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0,
                             sizeof(lightPush), &lightPush);
      lightCmd.draw(3, 1, 0, 0);
    }
    lightCmd.end();
    commandBuffer.executeCommands(lightCmd);
  } {
    // ImGUI Secondary Cmd record -> exec
    auto imguiCmd = m_imguiCommandBuffers[imageIndex].get();
    auto inheritanceInfo = vk::CommandBufferInheritanceInfo(m_renderPass, 1, m_framebuffers[imageIndex]);
    auto imguiBeginInfo = vk::CommandBufferBeginInfo(
      vk::CommandBufferUsageFlagBits::eRenderPassContinue | vk::CommandBufferUsageFlagBits::eSimultaneousUse,
      &inheritanceInfo);
    imguiCmd.reset();
    imguiCmd.begin(imguiBeginInfo); {
      TracyVkZone(m_vkContext, imguiCmd, "Imgui");
      ImGui_ImplVulkan_RenderDrawData(draw_data, imguiCmd);
    }
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
  createColorObjets();
  createDepthObjets();
  createDescriptorSet();
  m_texManager->updateDS(m_cfrDescriptorSet);
  createPipeline();
  createFramebuffers();
  createCommandBuffers();
}

void VkTestSiteApp::cleanupSwapchain() {
  m_uniforms.clear();
  m_cameraMultiple.clear();
  m_clustersCount.first.reset();
  m_clustersCount.second.reset();
  m_clustersIndices.first.reset();
  m_clustersIndices.second.reset();
  m_geometryDescriptorSet.destroy(m_device);
  m_clusterComputeDescriptorSet.destroy(m_device);
  m_cfrDescriptorSet.destroy(m_device);
  m_descriptorPool.destroy(m_device);
  m_device.freeCommandBuffers(m_commandPool, m_commandBuffers);
  m_depth.reset();
  for (const auto framebuffer: m_framebuffers) {
    m_device.destroyFramebuffer(framebuffer);
  }
  m_device.destroyPipeline(m_preDepthPipeline);
  m_device.destroyPipeline(m_clusterComputePipeline);
  m_device.destroyPipeline(m_cfrPipeline);
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
  m_textureWorkerPool.reset();
  m_lightManager.reset();
  m_transferThread.reset();
  m_stagingBuffer.reset();
  m_imguiCommandBuffers.clear();
  m_lightingCommandBuffers.clear();
  m_device.destroyCommandPool(m_commandPool);
  vmaDestroyAllocator(m_allocator);
  m_device.destroy();
  m_instance.destroySurfaceKHR(m_surface.release());
#ifndef NDEBUG
  m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
#endif
  m_instance.destroy();
}
