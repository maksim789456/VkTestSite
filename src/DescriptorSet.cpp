#include "DescriptorSet.h"

DescriptorSet::DescriptorSet() = default;

DescriptorSet::DescriptorSet(
  const vk::Device &device,
  const vk::DescriptorPool &descriptorPool,
  const uint32_t descriptorSetCount,
  const std::vector<DescriptorLayout> &layouts,
  const std::vector<vk::PushConstantRange> &push_consts
) {
  m_descriptorSetCount = descriptorSetCount;
  setup_layout(device, layouts, push_consts);
  create(device, descriptorPool);
}

/**
 * Create descriptor set layout and pipeline layout by provided DS layouts
 * @param device refence to logical device
 * @param layouts DS layouts
 * @param push_consts Push constants info
 */
void DescriptorSet::setup_layout(
  const vk::Device &device,
  const std::vector<DescriptorLayout> &layouts,
  const std::vector<vk::PushConstantRange> &push_consts
) {
  m_descriptorLayouts = layouts;

  auto layoutBindingsFlags = std::vector<vk::DescriptorBindingFlags>();
  auto layoutBindingsAllFlags = vk::DescriptorBindingFlags{};
  auto layoutBindings = std::vector<vk::DescriptorSetLayoutBinding>();
  for (const auto &layout: m_descriptorLayouts) {
    layoutBindingsAllFlags |= layout.bindingFlags;
    layoutBindingsFlags.emplace_back(layout.bindingFlags);
    layoutBindings.emplace_back(
      layout.shaderBinding, layout.type, layout.count, layout.stage
    );
  }

  const auto dslFlags =
      layoutBindingsAllFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind
        ? vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool
        : vk::DescriptorSetLayoutCreateFlags{};

  auto dslInfo = vk::DescriptorSetLayoutCreateInfo(dslFlags, layoutBindings);
  const auto flagsInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo(layoutBindingsFlags);
  dslInfo.pNext = &flagsInfo;
  m_descriptorSetLayout = device.createDescriptorSetLayout(dslInfo);

  const auto plInfo = vk::PipelineLayoutCreateInfo({}, m_descriptorSetLayout, push_consts);
  m_pipelineLayout = device.createPipelineLayout(plInfo);
}

/**
 * Allocate descriptor sets - count as swapchain images count
 * by descriptor set layout created on setup_layout
 * @param device refence to logical device
 * @param descriptorPool refence to descriptor pool, pool must contain an enough items
 */
void DescriptorSet::create(const vk::Device &device, const vk::DescriptorPool &descriptorPool) {
  const auto setLayouts = std::vector(m_descriptorSetCount, m_descriptorSetLayout);
  const auto info = vk::DescriptorSetAllocateInfo(descriptorPool, setLayouts);
  m_descriptorSets = device.allocateDescriptorSets(info);

  auto descriptorWrites = std::vector<vk::WriteDescriptorSet>();
  for (uint32_t i = 0; i < m_descriptorSetCount; i++) {
    descriptorWrites.clear();
    const auto &descriptorSet = m_descriptorSets[i];
    for (const auto &layout: m_descriptorLayouts) {
      if (layout.type == vk::DescriptorType::eUniformBuffer ||
          layout.type == vk::DescriptorType::eStorageBuffer) {
        auto writeInfo = vk::WriteDescriptorSet(
          descriptorSet, layout.shaderBinding, {}, layout.count, layout.type,
          {}, &layout.bufferInfos.at(i));
        descriptorWrites.push_back(writeInfo);
      } else if (layout.type == vk::DescriptorType::eCombinedImageSampler ||
                 layout.type == vk::DescriptorType::eInputAttachment) {
        try {
          for (int y = 0; y < layout.count; y++) {
            auto writeInfo = vk::WriteDescriptorSet(
              descriptorSet, layout.shaderBinding, {}, layout.count, layout.type,
              &layout.imageInfos.at(y));
            descriptorWrites.push_back(writeInfo);
          }
        } catch (std::out_of_range &_) {
        }
      }
    }

    for (const auto &descriptorWrite: descriptorWrites) {
      device.updateDescriptorSets(descriptorWrite, {});
    }
  }
}

void DescriptorSet::bind(
  const vk::CommandBuffer &commandBuffer,
  const uint32_t currentFrameIdx,
  const std::vector<uint32_t> &dynamicOffsets
) const {
  commandBuffer.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    m_pipelineLayout,
    0,
    m_descriptorSets[currentFrameIdx],
    dynamicOffsets
  );
}

void DescriptorSet::updateTexture(
  const vk::Device &device,
  const uint32_t shaderBinding,
  const uint32_t textureIndex,
  const vk::DescriptorImageInfo &imageInfo
) const {
  for (auto &descriptorSet: m_descriptorSets) {
    const auto write = vk::WriteDescriptorSet(
      descriptorSet,
      shaderBinding,
      textureIndex,
      1, vk::DescriptorType::eCombinedImageSampler,
      &imageInfo);

    device.updateDescriptorSets(write, {});
  }
}

const vk::PipelineLayout &DescriptorSet::getPipelineLayout() const {
  return m_pipelineLayout;
}

void DescriptorSet::destroy(const vk::Device &device) const {
  device.destroyPipelineLayout(m_pipelineLayout);
  device.destroyDescriptorSetLayout(m_descriptorSetLayout);
}
