#pragma once

template<typename UBO>
UniformBuffer<UBO>::UniformBuffer(const vma::Allocator allocator, vk::Flags<vk::MemoryPropertyFlagBits> flags) {
  this->allocator = allocator;
  bufferSize = sizeof(UBO);

  const auto bufferCreateInfo = vk::BufferCreateInfo({}, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer);
  const auto allocInfo = vma::AllocationCreateInfo(
    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
    vma::MemoryUsage::eAutoPreferDevice,
    flags
  );
  auto [buffer, alloc] = allocator.createBuffer(bufferCreateInfo, allocInfo);
  uniformBuffer = buffer;
  uniformBufferAlloc = alloc;

  if (!uniformBuffer || !uniformBufferAlloc) {
    throw std::runtime_error("Failed to create UniformBuffer!");
  }

  bufferInfo = vk::DescriptorBufferInfo(uniformBuffer, 0, bufferSize);
  uniformBufferMapped = allocator.mapMemory(uniformBufferAlloc);
}

template<typename UBO>
void UniformBuffer<UBO>::map(const UBO &ubo) {
  assert(bufferSize == sizeof(ubo));
  memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
}

template<typename UBO>
void UniformBuffer<UBO>::destroy() {
  if (uniformBufferAlloc) {
    allocator.unmapMemory(uniformBufferAlloc);
    uniformBufferMapped = nullptr;
  }
  if (uniformBuffer) {
    allocator.destroyBuffer(uniformBuffer, uniformBufferAlloc);
    uniformBuffer = nullptr;
    uniformBufferAlloc = nullptr;
  }
}
