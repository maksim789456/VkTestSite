#include "XrSystem.h"

#include <spdlog/spdlog.h>

const std::vector<std::string> XR_API_LAYERS = {};
const std::vector<std::string> XR_INSTANCE_EXTS = {};

vr::XrSystem::XrSystem(
  const vk::Instance instance,
  const vk::PhysicalDevice physicalDevice,
  const vk::Device device
) : vk_instance(instance), vk_physicalDevice(physicalDevice), vk_device(device) {
  const xr::ApplicationInfo appInfo(
    "VK Test Site", 1,
    "Some VK bullshit", 1,
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

  const xr::InstanceCreateInfo createInfo(
    {}, appInfo,
    activeLayers.size(), activeLayers.data(),
    activeExts.size(), activeExts.data()
  );

  xr_instance = xr::createInstanceUnique(&createInfo);

  xr::InstanceProperties instanceProperties = xr_instance->getInstanceProperties();
  spdlog::info("OpenXR Runtime: {} - {}", instanceProperties.runtimeName, XR_VERSION_PATCH(instanceProperties.runtimeVersion));
}
