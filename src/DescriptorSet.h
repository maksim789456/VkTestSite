#ifndef DESCRIPTORSET_H
#define DESCRIPTORSET_H

#include <vulkan/vulkan.hpp>
#include "utils.cpp"

struct DescriptorLayout {
  vk::DescriptorType type;
  vk::ShaderStageFlags stage;
  vk::DescriptorBindingFlags bindingFlags;
  uint32_t shaderBinding;
  uint32_t count;

  std::vector<vk::DescriptorImageInfo> imageInfos;
  std::vector<vk::DescriptorBufferInfo> bufferInfos;
};

class DescriptorSet {
public:
  DescriptorSet() = default;

  DescriptorSet(
    const vk::Device &device,
    const vk::DescriptorPool &descriptorPool,
    uint32_t descriptorSetCount,
    const std::vector<DescriptorLayout> &layouts,
    const std::vector<vk::PushConstantRange> &push_consts,
    const std::string &name = "Descriptor Set",
    vk::DescriptorSetLayoutCreateFlags dslFlags = {}
  );

  void bind(
    const vk::CommandBuffer &commandBuffer,
    uint32_t currentFrameIdx,
    const std::vector<uint32_t> &dynamicOffsets,
    const vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics
  ) const;

  void updateTexture(
    const vk::Device &device,
    uint32_t shaderBinding,
    uint32_t textureIndex,
    const vk::DescriptorImageInfo &imageInfo,
    const vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler
  ) const;

  [[nodiscard]] const vk::PipelineLayout &getPipelineLayout() const;

  void destroy(const vk::Device &device) const;

private:
  uint32_t m_descriptorSetCount = 0;
  std::vector<DescriptorLayout> m_descriptorLayouts;
  //std::vector<vk::DescriptorSetLayoutBinding> m_layoutsBindings;
  bool m_isPushDescriptor = false;

  vk::PipelineLayout m_pipelineLayout;
  vk::DescriptorSetLayout m_descriptorSetLayout;
  std::vector<vk::DescriptorSet> m_descriptorSets;
  std::vector<vk::WriteDescriptorSet> m_descriptorSetWrites;

  void setup_layout(
    const vk::Device &device,
    const std::vector<DescriptorLayout> &layouts,
    const std::vector<vk::PushConstantRange> &push_consts,
    const std::string &name,
    vk::DescriptorSetLayoutCreateFlags dslFlags = {}
  );

  void create(
    const vk::Device &device,
    const vk::DescriptorPool &descriptorPool,
    const std::string &name
  );
};

#endif //DESCRIPTORSET_H
