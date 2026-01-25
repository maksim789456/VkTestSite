#include "XrSystem.h"

#include <spdlog/spdlog.h>

const std::vector<std::string> XR_API_LAYERS = {
};
const std::vector<std::string> XR_INSTANCE_EXTS = {
  XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
#ifndef NDEBUG
  XR_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
};

vr::XrSystem::XrSystem() {
  auto appInfo = xr::ApplicationInfo(
    "VkTestSite", 1,
    "VkTestSite", 1,
    xr::Version(XR_API_VERSION_1_0)
  );

  auto activeLayers = std::vector<const char *>();
  auto activeExts = std::vector<const char *>();

  const auto availableLayers = xr::enumerateApiLayerPropertiesToVector();
  for (auto &requestLayer: XR_API_LAYERS) {
    for (auto &layerProperty: availableLayers) {
      if (strcmp(requestLayer.c_str(), layerProperty.layerName) != 0) {
        continue;
      }

      activeLayers.push_back(requestLayer.c_str());
      break;
    }
  }

  const auto availableExts = xr::enumerateInstanceExtensionPropertiesToVector(nullptr);
  for (auto &requestedInstanceExtension: XR_INSTANCE_EXTS) {
    bool found = false;
    for (auto &extensionProperty: availableExts) {
      if (strcmp(requestedInstanceExtension.c_str(), extensionProperty.extensionName) != 0) {
        continue;
      }

      activeExts.push_back(requestedInstanceExtension.c_str());
      found = true;
      break;
    }
    if (!found) {
      spdlog::error("Failed to find OpenXR instance extension: {}", requestedInstanceExtension);
    }
  }

  xr::InstanceCreateInfo ici(
    {}, appInfo,
    activeLayers.size(), activeLayers.data(),
    activeExts.size(), activeExts.data()
  );

#ifndef NDEBUG
  xr::DebugUtilsMessengerCreateInfoEXT debugCreateInfo(
    xr::DebugUtilsMessageSeverityFlagBitsEXT::AllBits,
    xr::DebugUtilsMessageTypeFlagBitsEXT::AllBits,
    OpenXRMessageCallbackFunction, nullptr
  );
  ici.next = &debugCreateInfo;
#endif

  try {
    xr_instance = xr::createInstanceUnique(ici);
  } catch (xr::exceptions::Error &error) {
    spdlog::error(std::format("OpenXR instance create error: {}", error.what()));
    return;
  }

  xr::InstanceProperties instanceProperties = xr_instance->getInstanceProperties();
  spdlog::info(
    "OpenXR Runtime: {} - {}.{}.{}",
    instanceProperties.runtimeName,
    XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
    XR_VERSION_MINOR(instanceProperties.runtimeVersion),
    XR_VERSION_PATCH(instanceProperties.runtimeVersion)
  );

#ifndef NDEBUG
  try {
    messenger = xr_instance->createDebugUtilsMessengerUniqueEXT(
      debugCreateInfo, getXRDispatch());
  } catch (xr::exceptions::Error &error) {
    spdlog::error(std::format("OpenXR debug messenger create error: {}", error.what()));
    return;
  }
#endif

  try {
    systemId = xr_instance->getSystem(xr::SystemGetInfo(xr::FormFactor::HeadMountedDisplay));
  } catch (xr::exceptions::Error &error) {
    spdlog::error(std::format("Cannot get OpenXR HMD system: {}", error.what()));
    return;
  }

  auto result = xr_instance->getSystemProperties(systemId, sysProps);
  if (result != xr::Result::Success) {
    spdlog::error(std::format("Failed to get system properties: {}", xr::to_string_literal(result)));
    return;
  }
  spdlog::info(std::format(
    "Current XR system: {} - {}x{}",
    sysProps.systemName,
    sysProps.graphicsProperties.maxSwapchainImageWidth,
    sysProps.graphicsProperties.maxSwapchainImageHeight
  ));
  ready = true;
}

vk::Instance vr::XrSystem::makeVkInstance(
  const vk::InstanceCreateInfo &createInfo
) {
  vk::Result result;
  vk::Instance instance;
  xr::VulkanInstanceCreateInfoKHR info;
  info.systemId = systemId;
  info.vulkanCreateInfo = reinterpret_cast<const VkInstanceCreateInfo *>(&createInfo);
  info.pfnGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
  xr_instance->createVulkanInstanceKHR(info, reinterpret_cast<VkInstance *>(&instance),
                                       reinterpret_cast<VkResult *>(&result), getXRDispatch());

  if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to create XR-compatible Vulkan instance");
  }

  return instance;
}

vk::PhysicalDevice vr::XrSystem::makeVkPhysicalDevice(const vk::Instance vkInstance) {
  xr::VulkanGraphicsDeviceGetInfoKHR getInfo;
  getInfo.systemId = systemId;
  getInfo.vulkanInstance = vkInstance;

  vk::PhysicalDevice physicalDevice;
  xr_instance->getVulkanGraphicsDevice2KHR(getInfo,
                                           reinterpret_cast<VkPhysicalDevice *>(&physicalDevice), getXRDispatch());
  assert(physicalDevice);

  return physicalDevice;
}

vk::Device vr::XrSystem::makeVkDevice(
  const vk::DeviceCreateInfo &createInfo,
  vk::PhysicalDevice physicalDevice
) {
  VkResult vkResult;
  VkDevice device;
  xr::VulkanDeviceCreateInfoKHR info;
  info.systemId = systemId;
  info.vulkanCreateInfo = reinterpret_cast<const VkDeviceCreateInfo *>(&createInfo);
  info.pfnGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;;
  info.vulkanPhysicalDevice = physicalDevice;

  XrResult result = getXRDispatch().xrCreateVulkanDeviceKHR(
    xr_instance.get(), info.get(), &device, &vkResult);

  if (result != XR_SUCCESS) {
    throw std::runtime_error("Failed to create XR-compatible Vulkan instance");
  }

  return device;
}

void vr::XrSystem::createSession(
  const vk::Instance vkInstance,
  const vk::PhysicalDevice physicalDevice,
  const vk::Device device,
  const uint32_t queueFamilyIndex,
  const uint32_t queueIndex
) {
  if (!ready) {
    spdlog::error("Xr system not ready, failed to create session");
    return;
  }

  m_vkInstance = vkInstance;
  m_device = device;
  m_physicalDevice = physicalDevice;

  xr::GraphicsRequirementsVulkanKHR vkGraphicsReq;
  xr_instance->getVulkanGraphicsRequirements2KHR(systemId, vkGraphicsReq, getXRDispatch());
  const auto vkVersion = xr::Version(1, 3, 0);
  const bool compatible = vkGraphicsReq.minApiVersionSupported <= vkVersion;
  if (!compatible) {
    spdlog::error(std::format("Incompatible Vulkan version, min supported {}",
                              vkGraphicsReq.minApiVersionSupported.get()));
    ready = false;
    return;
  }

  xr::GraphicsBindingVulkanKHR vkBinding;
  vkBinding.device = device;
  vkBinding.instance = vkInstance;
  vkBinding.physicalDevice = physicalDevice;
  vkBinding.queueFamilyIndex = queueFamilyIndex;
  vkBinding.queueIndex = queueIndex;

  xr::SessionCreateInfo sci({}, systemId);
  sci.next = &vkBinding;
  try {
    session = xr_instance->createSessionUnique(sci);
  } catch (xr::exceptions::Error &error) {
    spdlog::error(std::format("Cannot create XR session: {}", error.what()));
    ready = false;
    return;
  }

  xr::ReferenceSpaceCreateInfo refSpaceCI(xr::ReferenceSpaceType::Local, {});
  xrSpace = session->createReferenceSpaceUnique(refSpaceCI);

  refSpaceCI.referenceSpaceType = xr::ReferenceSpaceType::View;
  headSpace = session->createReferenceSpaceUnique(refSpaceCI);

  if (!findSwapchainFormat()) {
    spdlog::error("Unable to find VK swapchain format");
    ready = false;
    return;
  }
  createSwapchain();
}

bool vr::XrSystem::findSwapchainFormat() {
  auto swapchainFormats = session->enumerateSwapchainFormatsToVector();
  const auto it = std::ranges::find_if(
    swapchainFormats,
    [](auto format) {
      return format == static_cast<std::uint64_t>(vk::Format::eR8G8B8A8Unorm) ||
             format == static_cast<std::uint64_t>(vk::Format::eR8G8B8A8Srgb);
    }
  );

  if (it != swapchainFormats.end()) {
    swapchainFormat = static_cast<vk::Format>(*it);
    return true;
  }

  return false;
}

bool vr::XrSystem::createSwapchain() {
  auto viewConf = xr_instance->enumerateViewConfigurationViewsToVector(
    systemId, xr::ViewConfigurationType::PrimaryStereo);
  assert(viewConf.size() == 2);
  assert(viewConf[0].recommendedImageRectHeight == viewConf[1].recommendedImageRectHeight);

  fullSwapchainSize = vk::Extent2D(viewConf[0].recommendedImageRectWidth * 2,
                                   viewConf[1].recommendedImageRectHeight);
  eyeRenderSize = vk::Extent2D(viewConf[0].recommendedImageRectWidth, viewConf[1].recommendedImageRectHeight);

  xr::SwapchainCreateInfo swapchainCI;
  swapchainCI.width = eyeRenderSize.width; //TODO: fullSwapchainSize?
  swapchainCI.height = eyeRenderSize.height;
  swapchainCI.mipCount = 1;
  swapchainCI.sampleCount = 1;
  swapchainCI.arraySize = 2;
  swapchainCI.faceCount = 1;
  swapchainCI.format = static_cast<uint64_t>(swapchainFormat);
  swapchainCI.createFlags = xr::SwapchainCreateFlagBits::ProtectedContent;
  swapchainCI.usageFlags = xr::SwapchainUsageFlagBits::ColorAttachment
                           | xr::SwapchainUsageFlagBits::TransferSrc;
  try {
    swapchain = session->createSwapchainUnique(swapchainCI);
  } catch (xr::exceptions::Error &error) {
    spdlog::error(std::format("Cannot create XR swapchain: {}", error.what()));
    ready = false;
    return false;
  }

  const auto xrSwapchainImagesResult = enumerateSwapchainImagesToVector<xr::SwapchainImageVulkanKHR>(
    swapchain.get(), getXRDispatch());
  assert(xrSwapchainImagesResult.result == xr::Result::Success);

  swapchainImages =
      xrSwapchainImagesResult.value
      | std::views::transform([](auto const &xrSwapchainImage) {
        return vk::UniqueImage(xrSwapchainImage.image);
      })
      | std::ranges::to<std::vector>();

  swapchainImageViews = createSwapchainImageViewsUnique(
    m_device, swapchainImages, swapchainFormat, "XR Swapchain View", 2);

  //TODO: sync objs?

  return true;
}

void vr::XrSystem::pollEvents() {
  xr::EventDataBuffer event;
  xr::Result result;
  do {
    result = xr_instance->pollEvent(event);
    if (result == xr::Result::Success) {
      handleEvent(event);
    }
  } while (result == xr::Result::Success);
}

void vr::XrSystem::handleEvent(xr::EventDataBuffer event) {
  switch (event.type) {
    case xr::StructureType::EventDataSessionStateChanged: {
      const auto &sessionStateChanged = eventAs(xr::EventDataSessionStateChanged);
      spdlog::info("[XR] Session State Change: {}", xr::to_string(sessionStateChanged.state));
      sessionState = sessionStateChanged.state;
      switch (sessionState) {
        case xr::SessionState::Ready: {
          xr::SessionBeginInfo beginInfo = {};
          beginInfo.primaryViewConfigurationType = xr::ViewConfigurationType::PrimaryStereo;
          session->beginSession(beginInfo);
          sessionRunning = true;
          break;
        }
        case xr::SessionState::Stopping: {
          session->endSession();
          sessionRunning = false;
          break;
        }
        case xr::SessionState::Exiting:
        case xr::SessionState::LossPending: {
          sessionRunning = false;
          applicationRunning = false;
          break;
        }
        default: break;
      }
      break;
    }
    // Section: warnings
    case xr::StructureType::EventDataEventsLost: {
      const auto &eventsLost = eventAs(xr::EventDataEventsLost);
      spdlog::warn("[XR] Events Lost: {}", eventsLost.lostEventCount);
      break;
    }
    case xr::StructureType::EventDataInstanceLossPending: {
      const auto &instanceLoosPending = eventAs(xr::EventDataInstanceLossPending);
      spdlog::warn("[XR] Instance Loss Pending at: {}", instanceLoosPending.lossTime.get());
      break;
    }
    case xr::StructureType::EventDataInteractionProfileChanged: {
      const auto &interactionProfileChanged = eventAs(xr::EventDataInteractionProfileChanged);
      spdlog::warn("[XR] Interaction Profile changed for Session: {}",
                   fmt::ptr(interactionProfileChanged.session.get()));
      break;
    }
    case xr::StructureType::EventDataReferenceSpaceChangePending: {
      const auto &referenceSpaceChangePending = eventAs(xr::EventDataReferenceSpaceChangePending);
      spdlog::warn("[XR] Reference Space Change pending for Session: {}",
                   fmt::ptr(referenceSpaceChangePending.session.get()));
      break;
    }
    default:
      break;
  }
}

uint32_t vr::XrSystem::startFrame() {
  if (!sessionRunning) {
    return -1;
  }
  xrWaitFrame();
  xrBeginFrame(); {
    ZoneScopedN("XR Acquire swapchain");
    swapchainIdx = swapchain->acquireSwapchainImage({});
  } {
    ZoneScopedN("XR Wait swapchain");
    xr::SwapchainImageWaitInfo waitInfo = {};
    waitInfo.timeout = xr::Duration::infinite();
    swapchain->waitSwapchainImage(waitInfo);
  }
  return swapchainIdx;
}

void vr::XrSystem::endFrame() {
  xrEndFrame();
}

void vr::XrSystem::xrWaitFrame() {
  ZoneScopedN("XR Wait frame");
  const xr::FrameState state = session->waitFrame({});
  shouldRender = static_cast<bool>(state.shouldRender);
  predictedEndTime = state.predictedDisplayTime;

  TracyPlot("XR Display duration (ms)", state.predictedDisplayPeriod.get() / 1000000.0);

  xr::ViewState viewState = {};
  xr::ViewLocateInfo locateInfo = {};
  locateInfo.viewConfigurationType = xr::ViewConfigurationType::PrimaryStereo;
  locateInfo.displayTime = predictedEndTime;
  locateInfo.space = xrSpace.get(); {
    ZoneScopedN("Get views");
    xrViews = session->locateViewsToVector(locateInfo, viewState);
  }

  xr::SpaceLocation headLocation = headSpace->locateSpace(xrSpace.get(), predictedEndTime);
  headPosition = xrSpaceToVkSpace(toGlm(headLocation.pose.position));
  headRotation = xrSpaceToVkSpace(toGlm(headLocation.pose.orientation));

  auto updateCamera = [&](uint32_t idx, xr::View &view) {
    glm::vec3 translation{0.0f};
    auto tmp = makeXrViewMatrix(view.pose);
    // used to align with engine orientation
    //glm::mat4 correctOrientation = glm::rotate(-glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    eyeViews[idx] = glm::inverse(tmp); // * correctOrientation;// * glm::translate({}, translation);
    eyeProjections[idx] = makeXrProjectionMatrix(view.fov);
  };

  updateCamera(0, xrViews[0]);
  updateCamera(1, xrViews[1]);
}

void vr::XrSystem::xrBeginFrame() {
  ZoneScopedN("XR Begin frame");
  session->beginFrame({});
}

void vr::XrSystem::xrEndFrame() {
  ZoneScopedN("XR End frame"); {
    ZoneScopedN("XR Swapchain release");
    swapchain->releaseSwapchainImage({});
  }

  auto updateView = [&](uint32_t idx) {
    ZoneScopedN("Update eye");
    auto &view = xrProjViews[idx];
    view.pose = xrViews[idx].pose;
    view.fov = xrViews[idx].fov;

    xr::SwapchainSubImage &subImage = view.subImage;
    subImage.imageArrayIndex = idx;
    subImage.imageRect = xr::Rect2Di(xr::Offset2Di(), xr::Extent2Di(eyeRenderSize.width, eyeRenderSize.height));
    subImage.swapchain = swapchain.get();
  };

  updateView(0);
  updateView(1);

  xr::CompositionLayerProjection eyes;
  eyes.space = xrSpace.get();
  eyes.viewCount = 2;
  eyes.views = xrProjViews;

  const xr::CompositionLayerBaseHeader *const layers[] = {&eyes};

  xr::FrameEndInfo frameEndInfo;
  frameEndInfo.displayTime = predictedEndTime;
  frameEndInfo.environmentBlendMode = xr::EnvironmentBlendMode::Opaque;
  frameEndInfo.layerCount = 1;
  frameEndInfo.layers = layers; {
    ZoneScopedN("XR End Frame");
    session->endFrame(frameEndInfo);
  }
}
