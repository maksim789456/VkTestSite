#include "DescriptorSet.h"

DescriptorSet::DescriptorSet(
  const vk::Device &device,
  const vk::DescriptorPool &descriptorPool,
  const uint32_t descriptorSetCount,
  const std::vector<DescriptorLayout> &layouts,
  const std::vector<vk::PushConstantRange> &push_consts,
  const std::string &name,
  vk::DescriptorSetLayoutCreateFlags dslFlags
) {
  m_descriptorSetCount = descriptorSetCount;
  m_isPushDescriptor = static_cast<bool>(dslFlags & vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor);
  setup_layout(device, layouts, push_consts, name, dslFlags);
  create(device, descriptorPool, name);
}

/**
 * Create descriptor set layout and pipeline layout by provided DS layouts
 * @param device refence to logical device
 * @param layouts DS layouts
 * @param push_consts Push constants info
 * @param dslFlags DS layout create flags
 */
void DescriptorSet::setup_layout(
  const vk::Device &device,
  const std::vector<DescriptorLayout> &layouts,
  const std::vector<vk::PushConstantRange> &push_consts,
  const std::string &name,
  vk::DescriptorSetLayoutCreateFlags dslFlags
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

  dslFlags =
      layoutBindingsAllFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind
        ? dslFlags | vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool
        : dslFlags;

  auto dslInfo = vk::DescriptorSetLayoutCreateInfo(dslFlags, layoutBindings);
  const auto flagsInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo(layoutBindingsFlags);
  dslInfo.pNext = &flagsInfo;
  m_descriptorSetLayout = device.createDescriptorSetLayout(dslInfo);
  setObjectName(device, m_descriptorSetLayout, std::format("{} layout", name));

  const auto plInfo = vk::PipelineLayoutCreateInfo({}, m_descriptorSetLayout, push_consts);
  m_pipelineLayout = device.createPipelineLayout(plInfo);
  setObjectName(device, m_pipelineLayout, std::format("{} pipeline layout", name));
}

/**
 * Allocate descriptor sets - count as swapchain images count
 * by descriptor set layout created on setup_layout
 * @param device refence to logical device
 * @param descriptorPool refence to descriptor pool, pool must contain an enough items
 */
void DescriptorSet::create(
  const vk::Device &device,
  const vk::DescriptorPool &descriptorPool,
  const std::string &name
) {
  if (!m_isPushDescriptor) {
    const auto setLayouts = std::vector(m_descriptorSetCount, m_descriptorSetLayout);
    const auto info = vk::DescriptorSetAllocateInfo(descriptorPool, setLayouts);
    m_descriptorSets = device.allocateDescriptorSets(info);

    for (int i = 0; i < m_descriptorSetCount; ++i) {
      setObjectName(device, m_descriptorSets[i], std::format("{} {}", name, i));
    }
  }

  for (uint32_t i = 0; i < m_descriptorSetCount; i++) {
    m_descriptorSetWrites.clear();
    const auto &descriptorSet = !m_isPushDescriptor ? m_descriptorSets[i] : nullptr;
    for (const auto &layout: m_descriptorLayouts) {
      if (layout.type == vk::DescriptorType::eUniformBuffer ||
          layout.type == vk::DescriptorType::eStorageBuffer) {
        auto writeInfo = vk::WriteDescriptorSet(
          descriptorSet, layout.shaderBinding, {}, layout.count, layout.type,
          {}, &layout.bufferInfos.at(i));
        m_descriptorSetWrites.push_back(writeInfo);
      } else if (layout.type == vk::DescriptorType::eCombinedImageSampler ||
                 layout.type == vk::DescriptorType::eInputAttachment || layout.type ==
                 vk::DescriptorType::eStorageImage) {
        try {
          for (int y = 0; y < layout.count; y++) {
            auto writeInfo = vk::WriteDescriptorSet(
              descriptorSet, layout.shaderBinding, {}, layout.count, layout.type,
              &layout.imageInfos.at(y));
            m_descriptorSetWrites.push_back(writeInfo);
          }
        } catch (std::out_of_range &_) {
        }
      }
    }

    if (!m_isPushDescriptor) {
      for (const auto &descriptorWrite: m_descriptorSetWrites) {
        device.updateDescriptorSets(descriptorWrite, {});
      }
    }
  }
}

void DescriptorSet::bind(
  const vk::CommandBuffer &commandBuffer,
  const uint32_t currentFrameIdx,
  const std::vector<uint32_t> &dynamicOffsets,
  const vk::PipelineBindPoint bindPoint
) const {
  if (m_isPushDescriptor) {
    commandBuffer.pushDescriptorSetKHR(
      bindPoint,
      m_pipelineLayout,
      0,
      m_descriptorSetWrites
    );
  } else {
    commandBuffer.bindDescriptorSets(
      bindPoint,
      m_pipelineLayout,
      0,
      m_descriptorSets[currentFrameIdx],
      dynamicOffsets
    );
  }
}

void DescriptorSet::updateTexture(
  const vk::Device &device,
  const uint32_t shaderBinding,
  const uint32_t textureIndex,
  const vk::DescriptorImageInfo &imageInfo,
  const vk::DescriptorType type
) const {
  for (auto &descriptorSet: m_descriptorSets) {
    const auto write = vk::WriteDescriptorSet(
      descriptorSet,
      shaderBinding,
      textureIndex,
      1, type,
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
