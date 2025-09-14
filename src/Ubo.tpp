#pragma once

template<typename UBO>
UniformBuffer<UBO>::UniformBuffer(const vma::Allocator allocator, vk::Flags<vk::MemoryPropertyFlagBits> flags) {
  ZoneScoped;
  this->allocator = allocator;
  bufferSize = sizeof(UBO);

  std::tie(uniformBuffer, uniformBufferAlloc) =
      createBufferUnique(allocator, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
        vma::MemoryUsage::eAuto, vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);

  if (!uniformBuffer || !uniformBufferAlloc) {
    throw std::runtime_error("Failed to create UniformBuffer!");
  }

  bufferInfo = vk::DescriptorBufferInfo(uniformBuffer.get(), 0, bufferSize);
}

template<typename UBO>
void UniformBuffer<UBO>::map(const UBO &ubo) {
  ZoneScoped;
  assert(bufferSize == sizeof(ubo));
  uniformBufferMapped = allocator.mapMemory(uniformBufferAlloc.get());
  memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
  allocator.unmapMemory(uniformBufferAlloc.get());
}
