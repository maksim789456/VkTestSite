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
  std::cout << "Create mesh" << std::endl;
  m_indicesCount = indices.size();
  m_verticesCount = vertices.size();
  const auto verticesSize = vertices.size() * sizeof(VertexType);

  if (useStagingBuffer) {
    auto [stagingBuffer, stagingBufferAlloc] = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
    );

    fillBuffer(allocator, stagingBufferAlloc.get(), verticesSize, vertices);

    std::tie(m_verticesBuffer, m_verticesBufferAlloc) = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal
    );

    copyBuffer(device, graphicsQueue, commandPool, stagingBuffer.get(), m_verticesBuffer.get(), verticesSize);
  } else {
    std::tie(m_verticesBuffer, m_verticesBufferAlloc) = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
    );

    fillBuffer(allocator, m_verticesBufferAlloc.get(), verticesSize, vertices);
  }

  const auto indicesSize = indices.size() * sizeof(IndexType);
  if (useStagingBuffer) {
    auto [stagingBuffer, stagingBufferAlloc] = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
    );

    fillBuffer(allocator, stagingBufferAlloc.get(), indicesSize, indices);

    std::tie(m_indicesBuffer, m_indicesBufferAlloc) = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal
    );

    copyBuffer(device, graphicsQueue, commandPool, stagingBuffer.get(), m_indicesBuffer.get(), indicesSize);
  } else {
    std::tie(m_indicesBuffer, m_indicesBufferAlloc) = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eIndexBuffer,
      vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
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
