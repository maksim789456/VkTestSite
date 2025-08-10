#pragma once

#include <vulkan/vulkan.hpp>
#include <tracy/Tracy.hpp>
#include <assimp/scene.h>
#include <glm/ext/matrix_float4x4.hpp>

#include <functional>
#include <iostream>
#include <optional>
#include <ranges>
#include <vector>

std::vector<char const *> static gatherLayers(
  std::vector<std::string> const &layers
#ifndef NDEBUG
  , std::vector<vk::LayerProperties> const &layerProperties
#endif
) {
  ZoneScoped;
  std::vector<char const *> enabledLayers;
  enabledLayers.reserve(layers.size());

#ifndef NDEBUG
  const auto layerAvailable = [&](const std::string_view name) {
    return std::ranges::any_of(layerProperties, [&](const vk::LayerProperties &lp) {
      return std::string_view(lp.layerName.data()) == name;
    });
  };
#endif

  for (const auto &layer: layers) {
#ifndef NDEBUG
    assert(layerAvailable(layer));
#endif
    enabledLayers.push_back(layer.data());
  }

#ifndef NDEBUG
  constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
  if (std::ranges::none_of(layers, [&](const std::string &l) { return l == validationLayer; })
      && layerAvailable(validationLayer)) {
    enabledLayers.push_back(validationLayer.data());
  }
#endif

  return enabledLayers;
}

std::vector<char const *> static gatherExtensions(
  std::vector<std::string> const &extensions
#ifndef NDEBUG
  , std::vector<vk::ExtensionProperties> const &extensionProperties
#endif
) {
  ZoneScoped;
  std::vector<char const *> enabledExtensions;
  enabledExtensions.reserve(extensions.size());

#ifndef NDEBUG
  const auto extensionAvailable = [&](const std::string_view name) {
    return std::ranges::any_of(extensionProperties, [&](const vk::ExtensionProperties &lp) {
      return std::string_view(lp.extensionName.data()) == name;
    });
  };
#endif

  for (const auto &extension: extensions) {
#ifndef NDEBUG
    if (!extensionAvailable(extension)) {
      std::cerr << "Extension " << extension << " not available" << std::endl;
      abort();
    }
#endif
    enabledExtensions.push_back(extension.data());
  }

#ifndef NDEBUG
  constexpr std::string_view debugUtilsExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  if (std::ranges::none_of(extensions, [&](const std::string &l) { return l == debugUtilsExtension; })
      && extensionAvailable(debugUtilsExtension)) {
    enabledExtensions.push_back(debugUtilsExtension.data());
  }
#endif

  return enabledExtensions;
}

VKAPI_ATTR vk::Bool32 static VKAPI_CALL debugUtilsMessangerCallback(
  vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
  const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
  void *
) {
  std::cerr << vk::to_string(messageSeverity) << ": " << vk::to_string(messageTypes) << ":\n";
  std::cerr << std::string("\t") << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
  std::cerr << std::string("\t") << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
  std::cerr << std::string("\t") << "message         = <" << pCallbackData->pMessage << ">\n";
  if (0 < pCallbackData->queueLabelCount) {
    std::cerr << std::string("\t") << "Queue Labels:\n";
    for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++) {
      std::cerr << std::string("\t\t") << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
    }
  }
  if (0 < pCallbackData->cmdBufLabelCount) {
    std::cerr << std::string("\t") << "CommandBuffer Labels:\n";
    for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
      std::cerr << std::string("\t\t") << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
    }
  }
  if (0 < pCallbackData->objectCount) {
    std::cerr << std::string("\t") << "Objects:\n";
    for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
      std::cerr << std::string("\t\t") << "Object " << i << "\n";
      std::cerr << std::string("\t\t\t") << "objectType   = " << vk::to_string(pCallbackData->pObjects[i].objectType) <<
          "\n";
      std::cerr << std::string("\t\t\t") << "objectHandle = " << std::hex << pCallbackData->pObjects[i].objectHandle <<
          std::dec << "\n";
      if (pCallbackData->pObjects[i].pObjectName) {
        std::cerr << std::string("\t\t\t") << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
      }
    }
  }
  return vk::False;
}

#ifdef NDEBUG
vk::StructureChain<vk::InstanceCreateInfo>
#else
vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT>
#endif
static makeInstanceCreateInfoChain(
  vk::InstanceCreateFlagBits instanceCreateFlagBits,
  vk::ApplicationInfo const &applicationInfo,
  std::vector<const char *> const &layers,
  std::vector<const char *> const &extensions
) {
  ZoneScoped;
#ifdef NDEBUG
  vk::StructureChain<vk::InstanceCreateInfo> instanceCreateInfo({
    instanceCreateFlagBits, &applicationInfo, layers, extensions
  });
#else
  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

  vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> instanceCreateInfo(
    {instanceCreateFlagBits, &applicationInfo, layers, extensions},
    {{}, severityFlags, messageTypeFlags, &debugUtilsMessangerCallback}
  );
#endif
  return instanceCreateInfo;
}

std::optional<vk::PhysicalDevice> static pickPhysicalDevice(
  const vk::Instance &instance,
  const vk::SurfaceKHR &surface,
  const std::vector<const char *> &required_extensions
) {
  ZoneScoped;
  std::vector<vk::PhysicalDevice> physical_devices = instance.enumeratePhysicalDevices();

  if (physical_devices.empty()) {
    std::cerr << "No GPU's devices found" << std::endl;
    return std::nullopt;
  }

  auto check_device_extensions = [&](const vk::PhysicalDevice &device) {
    auto available_extensions = device.enumerateDeviceExtensionProperties();

    return std::ranges::all_of(required_extensions, [&](const auto &required) {
      return std::ranges::any_of(available_extensions, [&](const auto &available) {
        return std::string_view(available.extensionName) == required;
      });
    });
  };

  auto check_device_suitability = [&](const vk::PhysicalDevice &device) {
    const auto queue_families = device.getQueueFamilyProperties();
    bool has_graphics = false;
    bool has_present = false;

    uint32_t idx = 0;
    for (auto const queue_family: queue_families) {
      has_graphics = has_graphics || (queue_family.queueFlags & vk::QueueFlagBits::eGraphics);

      if (surface) {
        has_present = has_present || device.getSurfaceSupportKHR(idx, surface);
      }

      ++idx;
    }

    return has_graphics && (!surface || has_present);
  };

  for (const auto &device: physical_devices) {
    if (check_device_extensions(device) && check_device_suitability(device)) {
      return device;
    }
  }

  std::cerr << "Failed to found a suitable GPU!" << std::endl;
  return std::nullopt;
}

template<typename Func>
static void executeSingleTimeCommands(
  const vk::Device device,
  const vk::Queue queue,
  const vk::CommandPool commandPool,
  const Func&& executor
) {
  const auto allocInfo = vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
  const auto cmd = device.allocateCommandBuffers(allocInfo).front();

  constexpr auto beginInfo = vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
  cmd.begin(beginInfo);
  executor(cmd);
  cmd.end();

  auto submitInfo = vk::SubmitInfo{};
  submitInfo.setCommandBufferCount(1);
  submitInfo.setPCommandBuffers(&cmd);

  queue.submit(submitInfo);
  queue.waitIdle();

  device.freeCommandBuffers(commandPool, cmd);
}

static glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4& m) {
  return glm::mat4(
      m.a1, m.b1, m.c1, m.d1,
      m.a2, m.b2, m.c2, m.d2,
      m.a3, m.b3, m.c3, m.d3,
      m.a4, m.b4, m.c4, m.d4
  );
}