#pragma once

template<typename VertexType, typename IndexType>
Mesh<VertexType, IndexType>::Mesh(
  vma::Allocator allocator,
  const std::vector<VertexType> &vertices,
  const std::vector<IndexType> &indices,
  const bool useStagingBuffer) : m_useStaging(useStagingBuffer) {
  //TODO: Use staging copy buffer
  {
    const auto verticesSize = vertices.size() * sizeof(VertexType);
    std::tie(m_verticesBuffer, m_verticesBufferAlloc) = createBufferUnique(
      allocator,
      verticesSize,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
    );

    fillBuffer(allocator, m_verticesBufferAlloc.get(), verticesSize, vertices);

    m_verticesCount = vertices.size();
  } {
    const auto indicesSize = indices.size() * sizeof(IndexType);
    std::tie(m_indicesBuffer, m_indicesBufferAlloc) = createBufferUnique(
      allocator,
      indicesSize,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
      vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
    );

    fillBuffer(allocator, m_indicesBufferAlloc.get(), indicesSize, indices);

    m_indicesCount = indices.size();
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