#include "VkContext.h"

void vkts::VkContext::init(
  const VkContextConfig &config,
  GLFWwindow *window
) {
  ZoneScoped;
  m_window = window;
  m_enableValidation = config.enableValidation;
  m_enableDebug = config.enableDebug;

  m_loader = vk::detail::DynamicLoader();
  const auto vkGetInstanceProcAddr = m_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  createInstance(config);

  VkSurfaceKHR surface_tmp;
  glfwCreateWindowSurface(m_instance.get(), window, nullptr, &surface_tmp);
  m_surface = vk::UniqueSurfaceKHR(surface_tmp, m_instance.get());

  pickPhysicalDevice(config);
  createLogicalDevice(config);
  createQueues();
  createAllocator(config);
}

void vkts::VkContext::createInstance(const VkContextConfig &config) {
  ZoneScoped;

  const auto engineName = std::format("VkTs Engine ({})", VKTS_VERSION_STRING);
  spdlog::info(engineName);
  const vk::ApplicationInfo app_info(
    config.appName.c_str(), config.appVersion,
    engineName.c_str(), VKTS_VERSION,
    config.apiVersion
  );

  uint32_t glfwExtensionCount;
  const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<std::string> requiredExtensions(config.requiredInstanceExtensions.begin(),
                                              config.requiredInstanceExtensions.end());
  std::vector<std::string> requiredLayers(config.requiredValidationLayers.begin(),
                                          config.requiredValidationLayers.end());

  for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
    requiredExtensions.emplace_back(glfwExtensions[i]);
  }

  const auto enabled_extensions = gatherExtensions(
    requiredExtensions,
    vk::enumerateInstanceExtensionProperties(),
    config.enableDebug
  );
  const auto enabled_layers = gatherLayers(
    requiredLayers,
    vk::enumerateInstanceLayerProperties(),
    config.enableValidation
  );

  auto create_info = makeInstanceCreateInfoChain(
    {}, app_info,
    enabled_layers, enabled_extensions
  );
  m_instance = vk::createInstanceUnique(create_info.get<vk::InstanceCreateInfo>());
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance.get());

  if (config.enableDebug) {
    m_debugMessenger = m_instance->createDebugUtilsMessengerEXTUnique(
      create_info.get<vk::DebugUtilsMessengerCreateInfoEXT>());
  }
}

void vkts::VkContext::pickPhysicalDevice(const VkContextConfig &config) {
  const auto deviceTmp = pickPhysicalDeviceHelper(m_instance.get(), m_surface.get(), config.requiredDeviceExtensions);
  if (!deviceTmp) {
    abort();
  }
  m_physicalDevice = *deviceTmp;
  spdlog::info("Selected GPU: {}", std::string(m_physicalDevice.getProperties().deviceName));
}

void vkts::VkContext::createLogicalDevice(const VkContextConfig &config) {
  ZoneScoped;
  auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);

  std::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfos;
  std::vector<std::vector<float> > queuePrioritiesStorage;
  std::set queueFamilies = {indices.graphics, indices.present, indices.transfer};

  for (uint32_t queueFamilyIndex: queueFamilies) {
    uint32_t count = 1;
    if (queueFamilyIndex == indices.graphics && !indices.isTransferQueueSeparated()) {
      count = 2;
    }

    queuePrioritiesStorage.emplace_back(count, 1.0f);
    auto &priorities = queuePrioritiesStorage.back();

    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo
        .setQueueFamilyIndex(queueFamilyIndex)
        .setQueueCount(count)
        .setPQueuePriorities(priorities.data());
    deviceQueueCreateInfos.push_back(queueCreateInfo);
  }

  vk::PhysicalDeviceFeatures deviceFeatures{};
  vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
  vk::PhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{};
  vk::PhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures{};
  vk::PhysicalDeviceVulkan13Features features13{};

  hostQueryResetFeatures.setHostQueryReset(true);
  timelineSemaphoreFeatures
      .setTimelineSemaphore(true)
      .setPNext(&hostQueryResetFeatures);
  descriptorIndexingFeatures
      .setDescriptorBindingPartiallyBound(true)
      .setDescriptorBindingSampledImageUpdateAfterBind(true)
      .setShaderSampledImageArrayNonUniformIndexing(true)
      .setRuntimeDescriptorArray(true)
      .setDescriptorBindingVariableDescriptorCount(true)
      .setPNext(&timelineSemaphoreFeatures);

  features13
      .setSynchronization2(true)
      .setPNext(&descriptorIndexingFeatures);

  deviceFeatures
      .setSamplerAnisotropy(true)
      .setSampleRateShading(true);

  vk::DeviceCreateInfo device_create_info(
    {},
    deviceQueueCreateInfos,
    nullptr,
    config.requiredDeviceExtensions,
    &deviceFeatures
  );
  device_create_info.setPNext(&features13);

  m_device = m_physicalDevice.createDeviceUnique(device_create_info);
  if (!m_device) {
    throw std::runtime_error("Failed to create a vk logic device!");
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device.get());
}

void vkts::VkContext::createQueues() {
  ZoneScoped;
  const auto indices = QueueFamilyIndices(m_surface.get(), m_physicalDevice);
  m_graphicsQueue = std::make_unique<vk::Queue>(m_device->getQueue(indices.graphics, 0));
  m_presentQueue = std::make_unique<vk::Queue>(m_device->getQueue(indices.present, 0));
  m_transferQueue = std::make_unique<vk::Queue>(
    m_device->getQueue(indices.transfer, indices.isTransferQueueSeparated() ? 0 : 1));
}

void vkts::VkContext::createAllocator(const VkContextConfig &config) {
  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.flags = {};
  allocatorInfo.vulkanApiVersion = config.apiVersion;
  allocatorInfo.physicalDevice = m_physicalDevice;
  allocatorInfo.device = m_device.get();
  allocatorInfo.instance = m_instance.get();
  VmaAllocator vmaAllocator;
  auto allocCreateResult = vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
  if (allocCreateResult != VK_SUCCESS) {
    spdlog::error(std::format("vmaCreateAllocator failed with error code: {}",
                              vk::to_string(static_cast<vk::Result>(allocCreateResult))));
    throw std::runtime_error("Failed to create VMA allocator");
  }
  m_allocator = vma::UniqueAllocator(vmaAllocator);
}

vkts::VkContext::~VkContext() {
}

void vkts::VkContext::destroy() {
  spdlog::info("Context destroyed");
  auto allocator = m_allocator.release();
  vmaDestroyAllocator(allocator);
  m_device.reset();
  m_surface.reset();
  if (m_enableDebug) {
    m_debugMessenger.reset();
  }
  m_instance.reset();
}
