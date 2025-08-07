#pragma once

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"

#include <iostream>
#include <optional>
#include <ranges>
#include <vector>

static std::pair<vk::Buffer, vma::Allocation> createBuffer(
  const vma::Allocator allocator,
  const vk::DeviceSize size,
  const vk::BufferUsageFlags usage,
  const vk::MemoryPropertyFlags properties
) {
  const auto bufferCreateInfo = vk::BufferCreateInfo({}, size, usage, vk::SharingMode::eExclusive);
  const auto allocInfo = vma::AllocationCreateInfo(
    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
    vma::MemoryUsage::eAutoPreferDevice,
    properties
  );
  const auto pair = allocator.createBuffer(bufferCreateInfo, allocInfo);
  if (!pair.first || !pair.second) {
    throw std::runtime_error("Failed to create buffer!");
  }
  return pair;
}

static std::pair<vma::UniqueBuffer, vma::UniqueAllocation> createBufferUnique(
  const vma::Allocator allocator,
  const vk::DeviceSize size,
  const vk::BufferUsageFlags usage,
  const vk::MemoryPropertyFlags properties
) {
  const auto bufferCreateInfo = vk::BufferCreateInfo({}, size, usage, vk::SharingMode::eExclusive);
  const auto allocInfo = vma::AllocationCreateInfo(
    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
    vma::MemoryUsage::eAutoPreferDevice,
    properties
  );
  auto pair = allocator.createBufferUnique(bufferCreateInfo, allocInfo);
  if (!pair.first || !pair.second) {
    throw std::runtime_error("Failed to create buffer!");
  }

  return pair;
}

template<typename DataType>
static void fillBuffer(
  const vma::Allocator allocator,
  const vma::Allocation allocation,
  const vk::DeviceSize size,
  const std::vector<DataType> &data
) {
  const vk::DeviceSize dataSize = data.size() * sizeof(DataType);
  assert(dataSize <= size);

  const auto mapped = allocator.mapMemory(allocation);
  memcpy(mapped, data.data(), data.size());
  allocator.unmapMemory(allocation);
}
