#pragma once

#ifndef VKTESTSITE_XRSYSTEM_H
#define VKTESTSITE_XRSYSTEM_H

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.hpp>
#include <openxr/openxr_platform_defines.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>
#include <openxr/openxr.hpp>
#include "vr/XrUtils.cpp"
#include "utils.cpp"

#define eventAs(type) (*reinterpret_cast<type *>(&event))

namespace vr {
  class XrSystem {
  public:
    XrSystem();

    XrSystem(const XrSystem &) = delete;

    void createSession(
      vk::Instance vkInstance,
      vk::PhysicalDevice physicalDevice,
      vk::Device device,
      uint32_t queueFamilyIndex,
      uint32_t queueIndex
    );

    bool isReady() const { return ready; }

    vk::Instance makeVkInstance(const vk::InstanceCreateInfo &createInfo);

    vk::PhysicalDevice makeVkPhysicalDevice(const vk::Instance vkInstance);

    vk::Device makeVkDevice(const vk::DeviceCreateInfo &createInfo,
                            vk::PhysicalDevice physicalDevice);

    void pollEvents();

    uint32_t startFrame();

    void endFrame();

    bool isReady() { return ready; }
    vk::Format getSwapchainFormat() const { return swapchainFormat; }
    vk::Extent2D getEyeSize() const { return eyeRenderSize; }

    std::vector<vk::UniqueImageView> swapchainImageViews;

  private:
    bool ready = false;
    xr::UniqueInstance xr_instance;
    xr::UniqueDynamicDebugUtilsMessengerEXT messenger;
    xr::SystemId systemId;
    xr::SystemProperties sysProps;

    vk::Instance m_vkInstance;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;

    xr::UniqueSession session;
    xr::UniqueSpace xrSpace;
    xr::UniqueSpace headSpace;

    vk::Extent2D fullSwapchainSize;
    vk::Extent2D eyeRenderSize;
    vk::Format swapchainFormat;
    xr::UniqueSwapchain swapchain;
    std::vector<vk::UniqueImage> swapchainImages;

    xr::SessionState sessionState = xr::SessionState::Unknown;
    bool sessionRunning = false;
    bool applicationRunning = false;

    bool shouldRender;
    xr::Time predictedEndTime;
    uint32_t swapchainIdx = -1;

    glm::vec3 headPosition{0.0f};
    glm::quat headRotation = glm::identity<glm::quat>();
    std::vector<xr::View> xrViews;
    xr::CompositionLayerProjectionView xrProjViews[2];

    xr::DispatchLoaderDynamic &getXRDispatch() {
      static xr::DispatchLoaderDynamic dispatch = xr::DispatchLoaderDynamic::createFullyPopulated(
        xr_instance.get(), &xrGetInstanceProcAddr);
      return dispatch;
    }

    bool findSwapchainFormat();

    bool createSwapchain();

    void handleEvent(xr::EventDataBuffer event);

    void xrWaitFrame();

    void xrBeginFrame();

    void xrEndFrame();
  };
}

#endif //VKTESTSITE_XRSYSTEM_H
