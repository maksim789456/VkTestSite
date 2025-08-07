#pragma once

template<typename UBO>
UniformBuffer<UBO>::UniformBuffer(const vma::Allocator allocator, vk::Flags<vk::MemoryPropertyFlagBits> flags) {
  ZoneScoped;
  this->allocator = allocator;
  bufferSize = sizeof(UBO);

  auto [buffer, alloc] = createBuffer(allocator, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, flags);
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
  ZoneScoped;
  assert(bufferSize == sizeof(ubo));
  memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
}

template<typename UBO>
void UniformBuffer<UBO>::destroy() {
  ZoneScoped;
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
