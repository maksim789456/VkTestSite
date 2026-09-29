#ifndef DESCRIPTORLAYOUT_H
#define DESCRIPTORLAYOUT_H

#include <vulkan/vulkan.hpp>

struct DescriptorLayout {
  vk::DescriptorType type;
  vk::ShaderStageFlags stage;
  vk::DescriptorBindingFlags bindingFlags;
  uint32_t shaderBinding;
  uint32_t count;

  std::vector<vk::DescriptorImageInfo> imageInfos;
  std::vector<vk::DescriptorBufferInfo> bufferInfos;
};

#endif //DESCRIPTORLAYOUT_H