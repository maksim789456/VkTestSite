#pragma once

#ifndef STAGINGBUFF_H
#define STAGINGBUFF_H

#include <vulkan/vulkan.hpp>
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"

#include "BufferUtils.cpp"

/**
 * @brief StagingBuffer providing a CPU-accessible buffer for
 * uploading GPU resources (vertex/index buffers, textures, etc.)
 *
 * Manages allocations within a single large host-visible buffer using
 * a VMA virtual block, supporting multiple in-flight GPU transfers simultaneously
 *
 * Allocation lifecycle:
 * 1. Call <code>StagingBuffer::tryAllocate</code> or
 * <code>StagingBuffer::allocateBlocking</code>
 * to reserve a contiguous region of the staging buffer
 * 2. Write a CPU-side data directly into the mapped pointer returned
 * in <code>StagingBuffer::Allocation.mapped</code>
 * 3. Record a Vulkan copy command (buffer -> buffer or buffer -> image)
 * 4. Call <code>StagingBuffer::trackAlloc</code> to mark the allocation
 * as "in-flight" and associate it with the next timeline value
 * 5. After GPU work completes, <code>StagingBuffer::pollReclaimed</code>
 * free the allocation for reuse
 */
class StagingBuffer {
public:
  struct Allocation {
    vk::DeviceSize size = 0;
    vk::DeviceSize offset = 0;
    vma::VirtualAllocation handle = nullptr;
    void *mapped = nullptr;
    uint64_t timelineValue = 0;
  };

  StagingBuffer(
    const vk::Device device,
    const vma::Allocator allocator,
    const vk::DeviceSize bufferSize
  ): m_device(device), m_allocator(allocator), m_bufferSize(bufferSize) {
    ZoneScoped;
    std::tie(m_buffer, m_bufferAlloc) = createBufferUnique(
      allocator,
      bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vma::MemoryUsage::eAuto,
      vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
    );

    m_mapped = allocator.mapMemory(m_bufferAlloc.get());
    if (!m_mapped) throw std::runtime_error("Failed to map staging memory");

    m_virtualBlock = vma::createVirtualBlockUnique(vma::VirtualBlockCreateInfo(bufferSize));

    const auto semaTypeInfo = vk::SemaphoreTypeCreateInfo(vk::SemaphoreType::eTimeline);
    const auto semaInfo = vk::SemaphoreCreateInfo({}, &semaTypeInfo);
    m_timeline = m_device.createSemaphoreUnique(semaInfo);
  }

  ~StagingBuffer() {
    ZoneScoped;
    std::scoped_lock lock(m_mutex);
    m_device.waitIdle();

    for (const auto &alloc: m_pending) {
      //TracySecureFree(alloc.handle);
      m_virtualBlock->virtualFree(alloc.handle);
    }
    m_pending.clear();
    for (const auto &alloc: m_transferring) {
      //TracySecureFree(alloc.handle);
      m_virtualBlock->virtualFree(alloc.handle);
    }
    m_transferring.clear();

    if (m_mapped) {
      m_allocator.unmapMemory(m_bufferAlloc.get());
      m_mapped = nullptr;
    }
  }

  /**
   * Attempt to allocate space from the staging buffer without blocking
   * @param size Number of bytes to allocate
   * @param alignment Alignment in bytes. Defaults sets to 256
   * @return Optional<Allocation>. Returns a valid allocation if space was available immediately; returns std::nullopt if no space could be reserved at the moment
   */
  std::optional<Allocation> tryAllocate(const vk::DeviceSize size, const vk::DeviceSize alignment = 256) {
    ZoneScoped;
    if (size > m_bufferSize)
      throw std::runtime_error("Try to allocate size > staging buffer size");

    const auto vci = vma::VirtualAllocationCreateInfo(size, alignment);
    vma::VirtualAllocation virtualAlloc = nullptr;
    vk::DeviceSize offset = 0;
    std::lock_guard lock(m_mutex);
    if (const auto result = m_virtualBlock->virtualAllocate(&vci, &virtualAlloc, &offset);
      result != vk::Result::eSuccess) {
      return std::nullopt;
    }

    const auto mappedPtr = static_cast<char *>(m_mapped) + offset;
    //TracySecureAllocN(virtualAlloc, size, "Staging buffer allocations");

    Allocation allocation;
    allocation.size = size;
    allocation.handle = virtualAlloc;
    allocation.offset = offset;
    allocation.mapped = mappedPtr;
    m_pending.push_back(allocation);
    return allocation;
  }

  /**
   * @brief Attempt to allocate space from the staging buffer blocking until success
   * @param size Number of bytes to allocate
   * @param alignment Alignment in bytes. Defaults sets to 256
   * @return Allocation. Guaranteed to succeed eventually, unless the requested size exceeds the total staging buffer capacity
   *
   * Behavior:
   *  - If space is available now, returns immediately.
   *  - If not, determines the oldest in-flight allocation (tracked with a
   *    timeline semaphore), and blocks the calling CPU thread using
   *    vk::Device::waitSemaphores until the GPU has completed that work.
   *  - Once GPU has signaled, freed ranges are reclaimed and allocation is retried.
   */
  Allocation allocateBlocking(const vk::DeviceSize size, const vk::DeviceSize alignment = 256) {
    ZoneScoped;
    while (true) {
      if (const auto alloc = tryAllocate(size, alignment)) {
        return *alloc;
      }

      uint64_t waitValue = 0; {
        std::unique_lock lock(m_mutex);
        if (!m_transferring.empty()) {
          waitValue = m_transferring.front().timelineValue;
          for (const auto &alloc: m_transferring) {
            waitValue = std::min(waitValue, alloc.timelineValue);
          }
        }
      }

      if (waitValue > 0) {
        const auto waitInfo = vk::SemaphoreWaitInfo({}, m_timeline.get(), waitValue);
        auto _ = m_device.waitSemaphores(waitInfo, UINT64_MAX);
        pollReclaimed();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  }

  /**
   * Starting tracking allocation
   * @remark Must be called after Queue submit with allocation semaphore value
   * @param alloc staging allocation
   */
  void trackAlloc(Allocation &alloc) {
    ZoneScoped;
    std::lock_guard lock(m_mutex);

    const auto it = std::ranges::find_if(
      m_pending, [&](const Allocation &a) { return a.handle == alloc.handle; });
    if (it == m_pending.end()) {
      throw std::runtime_error("Tried to track allocation does not exist");
    }

    const auto value = ++m_nextTimelineValue;
    alloc.timelineValue = value;
    m_transferring.push_back(alloc);
    m_pending.erase(it);
  }

  /**
   * Reclaim staging buffer ranges whose GPU transfers have competed
   */
  void pollReclaimed() {
    ZoneScoped;
    std::lock_guard lock(m_mutex);
    const auto completed = m_device.getSemaphoreCounterValue(m_timeline.get());

    for (int i = 0; i < m_transferring.size();) {
      if (m_transferring[i].timelineValue <= completed) {
        //TracyFree(m_transferring[i].handle);
        m_virtualBlock->virtualFree(m_transferring[i].handle);
        m_transferring.erase(m_transferring.begin() + i);
      } else {
        ++i;
      }
    }
  }

  [[nodiscard]] vk::SemaphoreSubmitInfo makeSignalInfo() const {
    return {m_timeline.get(), m_nextTimelineValue, vk::PipelineStageFlagBits2::eAllCommands};
  }

  [[nodiscard]] vk::Buffer getBuffer() const { return m_buffer.get(); }

private:
  const vk::Device m_device = nullptr;
  const vma::Allocator m_allocator = nullptr;
  const vk::DeviceSize m_bufferSize = 0;

  vma::UniqueBuffer m_buffer;
  vma::UniqueAllocation m_bufferAlloc;
  vma::UniqueVirtualBlock m_virtualBlock;
  void *m_mapped = nullptr;

  TracyLockableN(std::mutex, m_mutex, "Staging Buffer Mutex");
  std::vector<Allocation> m_pending;
  std::vector<Allocation> m_transferring;

  vk::UniqueSemaphore m_timeline;
  uint64_t m_nextTimelineValue = 0;
};

#endif //STAGINGBUFF_H
