#ifndef DESCRIPTORPOOL_H
#define DESCRIPTORPOOL_H

#include <vulkan/vulkan.hpp>

class DescriptorPool {
public:
  DescriptorPool() = default;

  explicit DescriptorPool(const vk::Device &device) {
    auto pool_sizes = std::vector{
      vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 1000),
      vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 1000)
    };

    const auto info = vk::DescriptorPoolCreateInfo(
      vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
      1000, pool_sizes);

    descriptorPool = device.createDescriptorPool(info);
  }

  [[nodiscard]] const vk::DescriptorPool &getDescriptorPool() const {
    return descriptorPool;
  }

  void destroy(const vk::Device &device) const {
    device.destroyDescriptorPool(descriptorPool);
  }

private:
  vk::DescriptorPool descriptorPool;
};

#endif //DESCRIPTORPOOL_H
