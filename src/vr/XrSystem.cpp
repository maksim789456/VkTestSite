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
    reinterpret_cast<VkPhysicalDevice*>(&physicalDevice), getXRDispatch());
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
  for (const auto &format: swapchainFormats) {
    if (static_cast<std::uint64_t>(vk::Format::eR8G8B8A8Unorm) == format
        || static_cast<std::uint64_t>(vk::Format::eR8G8B8A8Srgb) == format) {
      swapchainFormat = static_cast<vk::Format>(format);
      return true;
    }
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
  swapchainCI.usageFlags = xr::SwapchainUsageFlagBits::ColorAttachment;
  try {
    swapchain = session->createSwapchainUnique(swapchainCI);
  } catch (xr::exceptions::Error &error) {
    spdlog::error(std::format("Cannot create XR swapchain: {}", error.what()));
    ready = false;
    return false;
  }

  swapchainImages = enumerateSwapchainImagesToVector<xr::SwapchainImageVulkanKHR>
      (swapchain.get(), getXRDispatch()).value;

  return true;
}
