#ifndef UBO_H
#define UBO_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"

template<typename UBO>
class UniformBuffer {
public:
  UniformBuffer() = default;
  UniformBuffer(vma::Allocator allocator, vk::Flags<vk::MemoryPropertyFlagBits> properties);
  ~UniformBuffer() = default;

  UniformBuffer(const UniformBuffer&) = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;

  UniformBuffer(UniformBuffer&& other) noexcept {
    moveFrom(std::move(other));
  }

  UniformBuffer& operator=(UniformBuffer&& other) noexcept {
    if (this != &other) {
      moveFrom(std::move(other));
    }
    return *this;
  }

  [[nodiscard]] const vk::DescriptorBufferInfo& getBufferInfo() const { return bufferInfo; };
  void map(const UBO& ubo);
  void destroy();

  vk::DeviceSize bufferSize = 0;
  vk::DescriptorBufferInfo bufferInfo{};
private:
  void moveFrom(UniformBuffer&& other) {
    allocator = other.allocator;

    uniformBuffer = std::exchange(other.uniformBuffer, nullptr);
    uniformBufferAlloc = std::exchange(other.uniformBufferAlloc, nullptr);
    uniformBufferMapped = std::exchange(other.uniformBufferMapped, nullptr);
    bufferSize = other.bufferSize;
    bufferInfo = other.bufferInfo;
  }
  vma::Allocator allocator = nullptr;
  vk::Buffer uniformBuffer = nullptr;
  vma::Allocation uniformBufferAlloc = nullptr;
  void* uniformBufferMapped = nullptr;
};

#include "Ubo.tpp"

#endif //UBO_H
