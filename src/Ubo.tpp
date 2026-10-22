#pragma once

template<typename UBO>
UniformBuffer<UBO>::UniformBuffer(
  const vma::Allocator allocator,
  const uint32_t imagesCount,
  const vk::ShaderStageFlags shaderStage,
  const uint32_t shaderBinding
) {
  ZoneScoped;
  this->allocator = allocator;
  bufferSize = sizeof(UBO);

  buffers.resize(imagesCount);
  buffersAlloc.resize(imagesCount);
  buffersMapped.resize(imagesCount);
  buffersInfo.resize(imagesCount);

  for (int i = 0; i < imagesCount; ++i) {
    std::tie(buffersAlloc[i], buffers[i]) =
        createBufferUnique(allocator, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                           vma::MemoryUsage::eAuto,
                           vma::AllocationCreateFlagBits::eMapped |
                           vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);

    if (!buffers[i] || !buffersAlloc[i]) {
      throw std::runtime_error("Failed to create UniformBuffer!");
    }

    buffersInfo[i] = vk::DescriptorBufferInfo(buffers[i].get(), 0, bufferSize);
  }

  dsLayout = DescriptorLayout{
    .type = vk::DescriptorType::eUniformBuffer,
    .stage = shaderStage,
    .bindingFlags = {},
    .shaderBinding = shaderBinding,
    .count = 1,
    .imageInfos = {},
    .bufferInfos = buffersInfo
  };
}

template<typename UBO>
void UniformBuffer<UBO>::map(const uint32_t imageIdx, const UBO &ubo) {
  ZoneScoped;
  assert(bufferSize == sizeof(ubo));
  buffersMapped[imageIdx] = allocator.mapMemory(buffersAlloc[imageIdx].get());
  memcpy(buffersMapped[imageIdx], &ubo, sizeof(ubo));
  allocator.unmapMemory(buffersAlloc[imageIdx].get());
}
