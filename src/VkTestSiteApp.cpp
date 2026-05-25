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

  m_context->device().waitIdle();
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
  m_contextConfig = {
    .requiredDeviceExtensions = DEVICE_EXTENSIONS,
    .requiredValidationLayers = LAYERS,
  };

  m_context = std::make_unique<vkts::VkContext>();
  m_context->init(m_contextConfig, m_window);
  m_msaaSamples = findMaxMsaaSamples(m_context->physicalDevice());

  m_swapchain = Swapchain(m_context->surface(), m_context->device(), m_context->physicalDevice(), m_window);
  createRenderPass();
  createUniformBuffers();
  m_descriptorPool = DescriptorPool(m_context->device());
  m_lightManager = std::make_unique<LightManager>(
    m_context->allocator(), m_swapchain.imageViews.size());
  createCommandPool();
  createColorObjets();
  createDepthObjets();
  createDescriptorSet();
  createPipeline();
  const auto lightCmdsInfo = vk::CommandBufferAllocateInfo(
    m_commandPool, vk::CommandBufferLevel::eSecondary, m_swapchain.imageViews.size()
  );
  m_lightingCommandBuffers = m_context->device().allocateCommandBuffersUnique(lightCmdsInfo);
  createFramebuffers();
  createCommandBuffers();
  createSyncObjects();
  const auto indices = QueueFamilyIndices(m_context->surface(), m_context->physicalDevice());
  m_stagingBuffer = std::make_unique<StagingBuffer>(m_context->device(), m_context->allocator(), 128 * 1024 * 1024);
  // 64 MB
  m_transferThread = std::make_unique<TransferThread>(m_context->device(), m_context->transferQueue(), indices.transfer,
                                                      *m_stagingBuffer);
  m_textureWorkerPool = std::make_unique<TextureWorkerPool>(m_context->device(), m_context->allocator(),
                                                            *m_stagingBuffer, *m_transferThread);
  m_texManager = std::make_unique<TextureManager>(
    m_context->device(), m_context->graphicsQueue(), m_commandPool, *m_textureWorkerPool, m_geometryDescriptorSet, 1);

  m_camera = std::make_unique<Camera>(m_swapchain.extent); {
    ZoneScopedN("Setup window callbacks");
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
  }

#ifndef NDEBUG
  {
    ZoneScopedN("Setup tracy vk context");
    const auto gpdctd = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(vkGetInstanceProcAddr(
      m_context->instance(), "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
    const auto gct = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(vkGetDeviceProcAddr(
      m_context->device(), "vkGetCalibratedTimestampsEXT"));
    m_tracyCmdBuffer = m_context->device().allocateCommandBuffers(
      vk::CommandBufferAllocateInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, 1))[0];
    m_vkContext = tracy::CreateVkContext(m_context->physicalDevice(), m_context->device(), m_context->graphicsQueue(),
                                         m_tracyCmdBuffer, gpdctd, gct);
    const std::string contextName = "Graphics Queue";
    m_vkContext->Name(contextName.data(), contextName.size());
  }
#endif

  {
    ZoneScopedN("Setup ImGui Render");
    ImGui_ImplGlfw_InitForVulkan(m_window, true);
    ImGui_ImplVulkan_InitInfo vkInitInfo = {};
    vkInitInfo.ApiVersion = m_contextConfig.apiVersion;
    vkInitInfo.Instance = m_context->instance();
    vkInitInfo.PhysicalDevice = m_context->physicalDevice();
    vkInitInfo.Device = m_context->device();
    vkInitInfo.QueueFamily = indices.graphics;
    vkInitInfo.Queue = m_context->graphicsQueue();
    vkInitInfo.RenderPass = m_renderPass;
    vkInitInfo.MinImageCount = vkInitInfo.ImageCount = MAX_FRAME_IN_FLIGHT;
    vkInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vkInitInfo.Subpass = 1;
    vkInitInfo.DescriptorPoolSize = 100;
    vkInitInfo.CheckVkResultFn = [](const VkResult err) {
      if (err != VK_SUCCESS)
        spdlog::error(std::format("Imgui Vk Error: {}",
                                  vk::to_string(static_cast<vk::Result>(err))));
    };
    if (!ImGui_ImplVulkan_Init(&vkInitInfo)) {
      spdlog::error("Failed to initialize Imgui Vulkan render");
      abort();
    }
  }

  const auto imguiCmdsInfo = vk::CommandBufferAllocateInfo(
    m_commandPool, vk::CommandBufferLevel::eSecondary, m_swapchain.imageViews.size()
  );
  m_imguiCommandBuffers = m_context->device().allocateCommandBuffersUnique(imguiCmdsInfo);
}

void VkTestSiteApp::createRenderPass() {
  ZoneScoped;
  const auto attachments = {
    vk::AttachmentDescription( // Depth
      {}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilReadOnlyOptimal),
    vk::AttachmentDescription( // Albedo
      {}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal),
    vk::AttachmentDescription( // Normal
      {}, vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal),
    vk::AttachmentDescription( // Final color (swapchain)
      {}, m_swapchain.format, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR)
  };

  auto colorRefs = {
    vk::AttachmentReference{1, vk::ImageLayout::eColorAttachmentOptimal}, //Albedo
    vk::AttachmentReference{2, vk::ImageLayout::eColorAttachmentOptimal} // Normal
  };
  constexpr auto depthRef = vk::AttachmentReference{0, vk::ImageLayout::eDepthStencilAttachmentOptimal};
  auto subpass0 = vk::SubpassDescription(
    {}, vk::PipelineBindPoint::eGraphics,
    {}, colorRefs, {}, &depthRef
  );

  auto inputRefs = {
    vk::AttachmentReference{0, vk::ImageLayout::eShaderReadOnlyOptimal}, //Depth
    vk::AttachmentReference{1, vk::ImageLayout::eShaderReadOnlyOptimal}, //Albedo
    vk::AttachmentReference{2, vk::ImageLayout::eShaderReadOnlyOptimal} // Normal
  };
  constexpr auto colorRef = vk::AttachmentReference{3, vk::ImageLayout::eColorAttachmentOptimal};
  auto subpass1 = vk::SubpassDescription(
    {}, vk::PipelineBindPoint::eGraphics,
    inputRefs, colorRef
  );

  auto dependencies = {
    vk::SubpassDependency(
      vk::SubpassExternal, 0,
      vk::PipelineStageFlagBits::eBottomOfPipe,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
      vk::AccessFlagBits::eMemoryRead,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite),
    vk::SubpassDependency(
      0, 1,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
      vk::PipelineStageFlagBits::eFragmentShader,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
      vk::AccessFlagBits::eInputAttachmentRead),
  };

  std::vector subpasses = {subpass0, subpass1};
  const auto renderPassInfo = vk::RenderPassCreateInfo({}, attachments, subpasses, dependencies);

  m_renderPass = m_context->device().createRenderPass(renderPassInfo);
}

void VkTestSiteApp::createPipeline() {
  ZoneScoped;
  m_geometryPipeline = PipelineBuilder(
        m_context->device(),
        m_renderPass,
        m_geometryDescriptorSet.getPipelineLayout(),
        "../res/shaders/deferred/geometry.ep.slang.spv",
        "Geometry Pass Pipeline"
      )
      .withBindingDescriptions({Vertex::GetBindingDescription()})
      .withAttributeDescriptions({Vertex::GetAttributeDescriptions()})
      .withColorBlendAttachments({
        PipelineBuilder::makeDefaultColorAttachmentState(),
        PipelineBuilder::makeDefaultColorAttachmentState(),
      })
      .depthStencil(true, true, vk::CompareOp::eGreaterOrEqual)
      .withSubpass(0)
      .buildGraphics();

  m_lightingPipeline = PipelineBuilder(
        m_context->device(),
        m_renderPass,
        m_lightingDescriptorSet.getPipelineLayout(),
        "../res/shaders/deferred/light.ep.slang.spv",
        "Lighting Pass Pipeline"
      )
      .depthStencil(false, false, vk::CompareOp::eAlways)
      .withCullMode(vk::CullModeFlagBits::eNone)
      .withSubpass(1)
      .buildGraphics();
}

void VkTestSiteApp::createColorObjets() {
  m_albedo = std::make_unique<Texture>(
    m_context->device(), m_context->allocator(),
    m_swapchain.extent.width, m_swapchain.extent.height, 1,
    vk::Format::eR8G8B8A8Unorm,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eColor,
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
    false, "Albedo G-Buffer"
  );
  m_normal = std::make_unique<Texture>(
    m_context->device(), m_context->allocator(),
    m_swapchain.extent.width, m_swapchain.extent.height, 1,
    vk::Format::eR16G16B16A16Sfloat,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eColor,
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
    false, "Normal G-Buffer"
  );
}

void VkTestSiteApp::createDepthObjets() {
  constexpr auto depthFormat = vk::Format::eD32Sfloat;

  m_depth = std::make_unique<Texture>(
    m_context->device(), m_context->allocator(),
    m_swapchain.extent.width, m_swapchain.extent.height, 1,
    depthFormat,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eDepth,
    vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment,
    false, "Depth attachment"
  );

  transitionImageLayout(
    m_context->device(), m_context->graphicsQueue(), m_commandPool,
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
      m_albedo->getImageView(),
      m_normal->getImageView(),
      m_swapchain.imageViews[i]
    };

    auto framebufferInfo = vk::FramebufferCreateInfo(
      {}, m_renderPass, attachments,
      m_swapchain.extent.width, m_swapchain.extent.height, 1
    );
    m_framebuffers[i] = m_context->device().createFramebuffer(framebufferInfo);
  }
}

void VkTestSiteApp::createUniformBuffers() {
  ZoneScoped;
  for (size_t i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_uniform = std::make_unique<UniformBuffer<UniformBufferObject> >(
      m_context->allocator(),
      m_swapchain.imageViews.size(),
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
      0
    );
  }
}

void VkTestSiteApp::createDescriptorSet() {
  ZoneScoped;

  const auto lightsDescriptor = DescriptorLayout{
    .type = vk::DescriptorType::eStorageBuffer,
    .stage = vk::ShaderStageFlagBits::eFragment,
    .bindingFlags = {},
    .shaderBinding = 1,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = m_lightManager->getBufferInfos()
  };

  m_geometryDescriptorSet = DescriptorSet(
    m_context->device(), m_descriptorPool.getDescriptorPool(), m_swapchain.imageViews.size(),
    {
      m_uniform->getDescriptorLayout(),
      DescriptorLayout{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind,
        .shaderBinding = 1,
        .count = MAX_TEXTURE_PER_DESCRIPTOR,
        .imageInfos = {},
        .bufferInfos = {}
      }
    }, {
      vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(ModelPushConsts))
    });

  m_lightingDescriptorSet = DescriptorSet(
    m_context->device(), m_descriptorPool.getDescriptorPool(), m_swapchain.imageViews.size(),
    {
      m_uniform->getDescriptorLayout(),
      lightsDescriptor,
      DescriptorLayout{
        .type = vk::DescriptorType::eInputAttachment,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .bindingFlags = {},
        .shaderBinding = 2,
        .count = 1,
        .imageInfos = {
          vk::DescriptorImageInfo({}, m_depth->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        },
        .bufferInfos = {}
      },
      DescriptorLayout{
        .type = vk::DescriptorType::eInputAttachment,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .bindingFlags = {},
        .shaderBinding = 3,
        .count = 1,
        .imageInfos = {
          vk::DescriptorImageInfo({}, m_albedo->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        },
        .bufferInfos = {}
      },
      DescriptorLayout{
        .type = vk::DescriptorType::eInputAttachment,
        .stage = vk::ShaderStageFlagBits::eFragment,
        .bindingFlags = {},
        .shaderBinding = 4,
        .count = 1,
        .imageInfos = {
          vk::DescriptorImageInfo({}, m_normal->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        },
        .bufferInfos = {}
      },
    }, {
      vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, sizeof(LightPushConsts)) // Lights count
    });
}

void VkTestSiteApp::createCommandPool() {
  ZoneScoped;
  const auto indices = QueueFamilyIndices(m_context->surface(), m_context->physicalDevice());

  const auto poolInfo = vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, indices.graphics);
  m_commandPool = m_context->device().createCommandPool(poolInfo);
}

void VkTestSiteApp::createCommandBuffers() {
  ZoneScoped;
  const auto commandBufInfo = vk::CommandBufferAllocateInfo(m_commandPool, vk::CommandBufferLevel::ePrimary,
                                                            m_swapchain.imageViews.size());
  m_commandBuffers = m_context->device().allocateCommandBuffers(commandBufInfo);
}

void VkTestSiteApp::createSyncObjects() {
  constexpr auto fenceInfo = vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled);
  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_inFlight.push_back(m_context->device().createFence(fenceInfo));
    m_imageAvailable.push_back(m_context->device().createSemaphore(vk::SemaphoreCreateInfo()));
    m_renderFinished.push_back(m_context->device().createSemaphore(vk::SemaphoreCreateInfo()));
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
          m_context->device(), m_context->graphicsQueue(), m_commandPool, m_context->allocator(), *m_texManager,
          *m_lightManager, pathStr);
        m_model->createCommandBuffers(m_context->device(), m_commandPool, m_swapchain.imageViews.size());
        m_modelLoaded = true;
      }
    }
    if (m_modelLoaded && ImGui::Button("Unload model")) {
      m_model.reset();
      m_modelLoaded = false;
    }

    if (m_modelLoaded && ImGui::Button("Dump VMA stats")) {
      char *statsString = nullptr;
      vmaBuildStatsString(m_context->allocator(), &statsString, true); {
        std::ofstream outStats{"VmaStats.json"};
        outStats << statsString;
        spdlog::info("VMA stats json saved at VmaStats.json file");
      }
      vmaFreeStatsString(m_context->allocator(), statsString);
    }

    ImGui::Separator();
    ImGui::Text("Select G-Buffer Debug Output");
    ImGui::RadioButton("None", &m_debugView, 0);
    ImGui::RadioButton("Light", &m_debugView, 1);
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

    if (m_modelLoaded) {
      m_model->drawUI();
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
  auto _ = m_context->device().waitForFences(m_inFlight[m_currentFrame], true, UINT64_MAX);
  m_context->device().resetFences(m_inFlight[m_currentFrame]);

  uint32_t imageIndex;
  try {
    const auto acquireResult = m_context->device().acquireNextImageKHR(
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
  m_context->graphicsQueue().submit(submitInfo, m_inFlight[m_currentFrame]);

  executeSingleTimeCommands(m_context->device(), m_context->graphicsQueue(), m_commandPool,
                            [&](const vk::CommandBuffer cmd) {
                              //m_vkContext->Collect(cmd);
                            });

  const auto presentInfo = vk::PresentInfoKHR(m_renderFinished[m_currentFrame], m_swapchain.swapchain, imageIndex);
  vk::Result presentResult;
  try {
    presentResult = m_context->presentQueue().presentKHR(presentInfo);
  } catch (vk::OutOfDateKHRError &) {
    presentResult = vk::Result::eErrorOutOfDateKHR;
  } catch (vk::SystemError &) {
    throw std::runtime_error("Failed to present swapchain image!");
  }

  if (presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR) {
    recreateSwapchain();
    return;
  }

  m_context->presentQueue().waitIdle();

  m_currentFrame = imageIndex;
}

void VkTestSiteApp::updateUniformBuffer(uint32_t imageIndex) {
  auto ubo = UniformBufferObject{
    glm::vec4(m_camera->getViewPos(), 1.0f),
    m_camera->getViewProj(),
    m_camera->getInvViewProj(),
    static_cast<uint32_t>(m_debugView)
  };
  m_uniform->map(imageIndex, ubo);
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
  auto albedoClearValue = vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f));
  auto normalClearValue = vk::ClearValue(vk::ClearColorValue(0.5f, 0.5f, 1.0f, 1.0f));
  auto depthClearValue = vk::ClearValue(vk::ClearDepthStencilValue(0.0f, 0));
  auto clearValues = {depthClearValue, albedoClearValue, normalClearValue, colorClearValue};
  const auto beginInfo = vk::RenderPassBeginInfo(m_renderPass, m_framebuffers[imageIndex], renderArea, clearValues);

  commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eSecondaryCommandBuffers); {
    // Model temp render
    if (m_modelLoaded) {
      auto modelCmd = m_model->cmdDraw(
        *m_vkContext,
        m_framebuffers[imageIndex],
        m_renderPass,
        m_geometryPipeline,
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
      //TracyVkZone(m_vkContext, lightCmd, "Light Pass");
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
      //TracyVkZone(m_vkContext, imguiCmd, "Imgui");
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

  m_context->device().waitIdle();
  cleanupSwapchain();

  m_swapchain = Swapchain(m_context->surface(), m_context->device(), m_context->physicalDevice(), m_window);
  createRenderPass();
  createUniformBuffers();
  m_descriptorPool = DescriptorPool(m_context->device());
  createColorObjets();
  createDepthObjets();
  createDescriptorSet();
  m_texManager->updateDS(m_geometryDescriptorSet);
  createPipeline();
  createFramebuffers();
  createCommandBuffers();
}

void VkTestSiteApp::cleanupSwapchain() {
  m_uniform.reset();
  m_geometryDescriptorSet.destroy(m_context->device());
  m_lightingDescriptorSet.destroy(m_context->device());
  m_descriptorPool.destroy(m_context->device());
  m_context->device().freeCommandBuffers(m_commandPool, m_commandBuffers);
  m_depth.reset();
  m_albedo.reset();
  m_normal.reset();
  for (const auto framebuffer: m_framebuffers) {
    m_context->device().destroyFramebuffer(framebuffer);
  }
  m_context->device().destroyPipeline(m_geometryPipeline);
  m_context->device().destroyPipeline(m_lightingPipeline);
  m_context->device().destroyRenderPass(m_renderPass);
  m_swapchain.destroy(m_context->device());
}

void VkTestSiteApp::cleanup() {
  ZoneScoped;
#ifndef NDEBUG
  TracyVkDestroy(m_vkContext);
#endif

  for (int i = 0; i < m_swapchain.imageViews.size(); ++i) {
    m_context->device().destroyFence(m_inFlight[i]);
    m_context->device().destroySemaphore(m_imageAvailable[i]);
    m_context->device().destroySemaphore(m_renderFinished[i]);
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
  m_context->device().destroyCommandPool(m_commandPool);
  m_context->destroy();
}
