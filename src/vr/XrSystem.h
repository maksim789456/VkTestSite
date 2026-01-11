#pragma once

#ifndef VKTESTSITE_XRSYSTEM_H
#define VKTESTSITE_XRSYSTEM_H

#include <openxr/openxr.hpp>
#include <vulkan/vulkan.hpp>

namespace vr {
  class XrSystem {
  public:
    XrSystem(
      const vk::Instance instance,
      const vk::PhysicalDevice physicalDevice,
      const vk::Device device
    );

    XrSystem() = delete;
    XrSystem(const XrSystem &) = delete;

  private:
    vk::Instance vk_instance;
    vk::PhysicalDevice vk_physicalDevice;
    vk::Device vk_device;

    xr::UniqueInstance xr_instance;
  };
}

#endif //VKTESTSITE_XRSYSTEM_H
