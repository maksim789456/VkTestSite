#ifndef UBO_H
#define UBO_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include "BufferUtils.cpp"

template<typename UBO>
class UniformBuffer {
public:
  UniformBuffer() = default;
  UniformBuffer(vma::Allocator allocator, vk::Flags<vk::MemoryPropertyFlagBits> flags);
  ~UniformBuffer() = default;

  UniformBuffer(const UniformBuffer&) = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;

  UniformBuffer(UniformBuffer&&) noexcept = default;
  UniformBuffer& operator=(UniformBuffer&&) noexcept = default;

  [[nodiscard]] const vk::DescriptorBufferInfo& getBufferInfo() const { return bufferInfo; };
  void map(const UBO& ubo);

  vk::DeviceSize bufferSize = 0;
  vk::DescriptorBufferInfo bufferInfo{};
private:
  vma::Allocator allocator = nullptr;
  vma::UniqueBuffer uniformBuffer = {};
  vma::UniqueAllocation uniformBufferAlloc = {};
  void* uniformBufferMapped = nullptr;
};

#include "Ubo.tpp"

#endif //UBO_H
