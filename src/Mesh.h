#ifndef MESH_H
#define MESH_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
#include "BufferUtils.cpp"

template<typename VertexType, typename IndexType>
class Mesh {
public:
  Mesh() = default;

  Mesh(vma::Allocator allocator,
       const vk::Device device,
       const vk::Queue graphicsQueue,
       const vk::CommandPool commandPool,
       const std::vector<VertexType> &vertices,
       const std::vector<IndexType> &indices,
       bool useStagingBuffer = true);

  ~Mesh() = default;

  void update(
    const std::vector<VertexType> &vertices,
    const std::vector<IndexType> &indices
  );

  vk::Buffer getVertexBuffer() { return m_verticesBuffer.get(); }
  vk::Buffer getIndicesBuffer() { return m_indicesBuffer.get(); }
  size_t getIndicesCount() const { return m_indicesCount; }

private:
  vma::UniqueBuffer m_verticesBuffer = {};
  vma::UniqueAllocation m_verticesBufferAlloc = {};
  size_t m_verticesCount = 0;
  vma::UniqueBuffer m_indicesBuffer = {};
  vma::UniqueAllocation m_indicesBufferAlloc = {};
  size_t m_indicesCount = 0;

  bool m_useStaging = false;
};

#include "Mesh.tpp"

#endif //MESH_H
