#ifndef UBO_H
#define UBO_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include "BufferUtils.cpp"

template<typename UBO>
class UniformBuffer {
public:
  UniformBuffer(
    vma::Allocator allocator,
    uint32_t imagesCount,
    vk::ShaderStageFlags shaderStage,
    uint32_t shaderBinding
  );

  UniformBuffer() = delete;

  ~UniformBuffer() = default;

  [[nodiscard]] const vk::DescriptorBufferInfo &getBufferInfo(const uint32_t imageIdx) const { return buffersInfo[imageIdx]; };
  [[nodiscard]] const DescriptorLayout& getDescriptorLayout() const { return dsLayout; };

  void map(const uint32_t imageIdx, const UBO &ubo);

  vk::DeviceSize bufferSize = 0;

private:
  vma::Allocator allocator = nullptr;
  std::vector<vma::UniqueBuffer> buffers = {};
  std::vector<vma::UniqueAllocation> buffersAlloc = {};
  std::vector<void *> buffersMapped = {};
  std::vector<vk::DescriptorBufferInfo> buffersInfo = {};

  DescriptorLayout dsLayout = {};
};

#include "Ubo.tpp"

#endif //UBO_H
