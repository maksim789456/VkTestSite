#ifndef TRANSFERTHREAD_H
#define TRANSFERTHREAD_H

#include <deque>
#include <vulkan/vulkan.hpp>
#include "concurrentqueue/blockingconcurrentqueue.h"

#include "StagingBuffer.h"
#include "utils.cpp"

// TODO: More universal job struct to copy into buffer
struct TextureUploadJob {
  StagingBuffer::Allocation allocation;
  vk::Image dstImage;
  vk::ImageSubresourceRange subresourceRange;
  vk::BufferImageCopy region;
  vk::ImageLayout srcImageLayout = vk::ImageLayout::eUndefined;
  vk::ImageLayout dstImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

/**
 * Handles asynchronous GPU uploads of staging buffer allocations to textures
 * - Pulls completed jobs from job queues (e.g., future texture loaders)
 * - Records copy commands into a command buffer.
 * - Submits command buffer to a transfer-capable queue
 * - Uses semaphore signaling to track GPU completion of each allocation
 * - Polls staging buffer to reclaim memory after GPU finishes processing
 */
class TransferThread {
public:
  TransferThread(
    const vk::Device device,
    const vk::Queue transferQueue,
    const uint32_t transferQueueFamilyIndex,
    StagingBuffer &stagingBuffer
  ): m_device(device), m_transferQueue(transferQueue), m_stagingBuffer(stagingBuffer), m_stop(false) {
    ZoneScoped;
    const auto transferPoolInfo = vk::CommandPoolCreateInfo(
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer, transferQueueFamilyIndex);
    m_commandPool = m_device.createCommandPoolUnique(transferPoolInfo);
    const auto transferCmdInfo = vk::CommandBufferAllocateInfo(
      m_commandPool.get(), vk::CommandBufferLevel::ePrimary, 1);
    m_cmdBuff = std::move(m_device.allocateCommandBuffersUnique(transferCmdInfo).front());
    m_submitFence = m_device.createFenceUnique({});

    m_thread = std::thread(&TransferThread::threadLoop, this);
  }

  ~TransferThread() {
    m_stop = true;
    if (m_thread.joinable())
      m_thread.join();
  }

  TransferThread(const TransferThread &) = delete;

  TransferThread &operator=(const TransferThread &) = delete;

  void pushJob(const TextureUploadJob &job) {
    ZoneScoped;
    m_queue.enqueue(job);
  }

private:
  vk::Device m_device;
  vk::Queue m_transferQueue;
  StagingBuffer &m_stagingBuffer;
  vk::UniqueCommandPool m_commandPool;
  vk::UniqueCommandBuffer m_cmdBuff;
  vk::UniqueFence m_submitFence;

  moodycamel::BlockingConcurrentQueue<TextureUploadJob> m_queue;
  std::thread m_thread;
  std::atomic_bool m_stop;

  std::chrono::microseconds m_maxBatchWait = std::chrono::microseconds(2000);

  void threadLoop() {
    tracy::SetThreadName("VK Transfer Thread");
    while (!m_stop.load()) {
      if (TextureUploadJob firstJob{}; m_queue.wait_dequeue_timed(firstJob, m_maxBatchWait)) {
        ZoneScoped;
        std::deque<TextureUploadJob> batch;
        batch.push_back(firstJob);

        TextureUploadJob nextJob{};
        while (m_queue.try_dequeue(nextJob)) {
          batch.push_back(nextJob);
        }

        if (!batch.empty()) {
          recordAndSubmitBatch(batch);
        }
      }
    }
  }

  void recordAndSubmitBatch(std::deque<TextureUploadJob> &batch) {
    ZoneScoped;
    m_device.resetFences(*m_submitFence);
    m_cmdBuff->reset();
    m_cmdBuff->begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    for (auto &job: batch) {
      ZoneScopedN("Record cmd's for job");
      cmdTransitionImageLayout2(
        m_cmdBuff.get(),
        job.dstImage,
        job.srcImageLayout,
        vk::ImageLayout::eTransferDstOptimal,
        job.subresourceRange
      );

      m_cmdBuff->copyBufferToImage(
        m_stagingBuffer.getBuffer(), job.dstImage,
        vk::ImageLayout::eTransferDstOptimal, job.region);

      cmdTransitionImageLayout2(
        m_cmdBuff.get(),
        job.dstImage,
        vk::ImageLayout::eTransferDstOptimal,
        job.dstImageLayout,
        job.subresourceRange
      );
    }

    m_cmdBuff->end();

    for (auto &job: batch) {
      m_stagingBuffer.trackAlloc(job.allocation);
    } {
      ZoneScopedN("Queue Submit");
      const auto cbSubmitInfo = vk::CommandBufferSubmitInfo(m_cmdBuff.get());
      const auto sigInfo = m_stagingBuffer.makeSignalInfo();
      const auto submit = vk::SubmitInfo2({}, {}, cbSubmitInfo, sigInfo);

      m_transferQueue.submit2(submit, *m_submitFence);
    } {
      ZoneScopedN("Wait Queue");
      auto _ = m_device.waitForFences(*m_submitFence, VK_TRUE, UINT64_MAX);
    }

    m_stagingBuffer.pollReclaimed();
  }
};

#endif //TRANSFERTHREAD_H
