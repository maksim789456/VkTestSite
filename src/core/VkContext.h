#ifndef VKTESTSITE_VKCONTEXT_H
#define VKTESTSITE_VKCONTEXT_H

#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include <tracy/Tracy.hpp>
#include <spdlog/spdlog.h>

#include "QueueFamilyIndices.cpp"
#include "utils.cpp"
#include "version.h"

#include <set>

namespace vkts {
  struct VkContextConfig {
    std::string appName = "VkTs App";
    uint32_t appVersion = VK_MAKE_VERSION(1, 0, 0);
    uint32_t apiVersion = VK_API_VERSION_1_3;
    bool enableDebug = true;
    bool enableValidation = true;
    std::vector<const char *> requiredInstanceExtensions;
    std::vector<const char *> requiredDeviceExtensions;
    std::vector<const char *> requiredValidationLayers = {
#ifndef NDEBUG
      "VK_LAYER_KHRONOS_validation"
#endif
    };
  };

  class VkContext {
  public:
    VkContext() = default;

    ~VkContext();

    void init(
      const VkContextConfig &config,
      GLFWwindow *window
    );

    void destroy();

    [[nodiscard]] vk::Instance instance() const { return m_instance.get(); }
    [[nodiscard]] vk::PhysicalDevice physicalDevice() const { return m_physicalDevice; }
    [[nodiscard]] vk::Device device() const { return m_device.get(); }
    [[nodiscard]] vma::Allocator allocator() const { return m_allocator.get(); }
    [[nodiscard]] vk::SurfaceKHR surface() const { return m_surface.get(); }
    [[nodiscard]] vk::Queue &graphicsQueue() const { return *m_graphicsQueue; }
    [[nodiscard]] vk::Queue &transferQueue() const { return *m_transferQueue; }
    [[nodiscard]] vk::Queue &presentQueue() const { return *m_presentQueue; }
    [[nodiscard]] GLFWwindow &window() const { return *m_window; }

    [[nodiscard]] bool hasValidation() const { return m_enableValidation; }
    [[nodiscard]] bool isDebug() const { return m_enableDebug; }

  private:
    void createInstance(const VkContextConfig &config);

    void pickPhysicalDevice(const VkContextConfig &config);

    void createLogicalDevice(const VkContextConfig &config);

    void createQueues();

    void createAllocator(const VkContextConfig &config);

    GLFWwindow* m_window = nullptr;

    vk::detail::DynamicLoader m_loader;
    vk::UniqueInstance m_instance;
    vk::PhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    vk::UniqueDevice m_device;
    vk::UniqueSurfaceKHR m_surface;
    vma::UniqueAllocator m_allocator;

    std::unique_ptr<vk::Queue> m_graphicsQueue;
    std::unique_ptr<vk::Queue> m_presentQueue;
    std::unique_ptr<vk::Queue> m_transferQueue;

    vk::UniqueDebugUtilsMessengerEXT m_debugMessenger;
    bool m_enableDebug = false;
    bool m_enableValidation = false;
  };
}

#endif //VKTESTSITE_VKCONTEXT_H
