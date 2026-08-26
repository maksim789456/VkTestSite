#pragma once

template<typename VertexType, typename IndexType>
Mesh<VertexType, IndexType>::Mesh(
  vma::Allocator allocator,
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  const std::vector<VertexType> &vertices,
  const std::vector<IndexType> &indices,
  const bool useStagingBuffer) : m_useStaging(useStagingBuffer) {
  m_indicesCount = indices.size();
  m_verticesCount = vertices.size();
  spdlog::info("Create mesh (vert: {}; indices: {})", m_verticesCount, m_indicesCount);
  const auto verticesSize = m_verticesCount * sizeof(VertexType);

  if (useStagingBuffer) {
    auto [stagingBufferAlloc, stagingBuffer] = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vma::MemoryUsage::eAuto, vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
    );

    fillBuffer(allocator, stagingBufferAlloc.get(), verticesSize, vertices);

    std::tie(m_verticesBufferAlloc, m_verticesBuffer) = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
      vma::MemoryUsage::eGpuOnly
    );

    copyBuffer(device, graphicsQueue, commandPool, stagingBuffer.get(), m_verticesBuffer.get(), verticesSize);
  } else {
    std::tie(m_verticesBufferAlloc, m_verticesBuffer) = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eVertexBuffer,
      vma::MemoryUsage::eAuto,
      vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
    );

    fillBuffer(allocator, m_verticesBufferAlloc.get(), verticesSize, vertices);
  }

  const auto indicesSize = indices.size() * sizeof(IndexType);
  if (useStagingBuffer) {
    auto [stagingBufferAlloc, stagingBuffer] = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vma::MemoryUsage::eAuto,
      vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
    );

    fillBuffer(allocator, stagingBufferAlloc.get(), indicesSize, indices);

    std::tie(m_indicesBufferAlloc, m_indicesBuffer) = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
      vma::MemoryUsage::eGpuOnly
    );

    copyBuffer(device, graphicsQueue, commandPool, stagingBuffer.get(), m_indicesBuffer.get(), indicesSize);
  } else {
    std::tie(m_indicesBufferAlloc, m_indicesBuffer) = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eIndexBuffer,
      vma::MemoryUsage::eAuto,
      vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
    );

    fillBuffer(allocator, m_indicesBufferAlloc.get(), indicesSize, indices);
  }
}

template<typename VertexType, typename IndexType>
void Mesh<VertexType, IndexType>::update(
  const std::vector<VertexType> &vertices,
  const std::vector<IndexType> &indices
) {
  if (m_useStaging || true) {
    throw std::runtime_error("Cannot update device only buffers!");
  }
}
