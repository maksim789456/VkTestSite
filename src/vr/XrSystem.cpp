#include "XrSystem.h"

vr::XrSystem::XrSystem(
  const vk::Instance instance,
  const vk::PhysicalDevice physicalDevice,
  const vk::Device device
) : vk_instance(instance), vk_physicalDevice(physicalDevice), vk_device(device) {
  const xr::ApplicationInfo appInfo(
    "VK Test Site", 1,
    "Some VK bullshit", 1,
    XR_CURRENT_API_VERSION
  );
  auto layers = std::vector<const char *>();
  auto exts = std::vector<const char *>();

  const xr::InstanceCreateInfo createInfo(
    {}, appInfo,
    layers.size(), layers.data(),
    exts.size(), exts.data()
  );

  xr_instance = xr::createInstanceUnique(&createInfo);
}
