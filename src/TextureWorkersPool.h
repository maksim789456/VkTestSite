#ifndef TEXTUREWORKERSPOOL_H
#define TEXTUREWORKERSPOOL_H

#include <vulkan/vulkan.hpp>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <stb_image.h>
#include "spdlog/mdc.h"

#include <ktx.h>
#include <ktxvulkan.h>

#include "StagingBuffer.h"
#include "TransferThread.h"
#include "Texture.h"
#include <filesystem>

struct TextureLoadJob {
  uint32_t texIndex = UINT32_MAX;
  std::filesystem::path filepath;
};

struct TextureLoadDone {
  TextureLoadJob job;
  std::unique_ptr<Texture> texture;
};

/**
 * @brief Thread pool for async loading of textures and place upload jobs
 *
 * Handles generic and KTX/KTX2 textures formats.
 *
 * Loading lifecycle:
 * 1. Fill <code>TextureLoadJob</code> and pass it into
 * <code>TextureWorkerPool::pushJob</code>
 * 2. Next available thread from pool dequeue job, loading by stb_image or
 * ktx library (depending on the extension), get allocation from staging buffer,
 * copy into staging buffer and place upload job into transfer thread
 * 3. After texture loading completed <code>TextureLoadDone</code> placed at
 * done queue and ready texture can get by call <code>TextureWorkerPool::tryDequeueDone</code>
 */
class TextureWorkerPool {
public:
  TextureWorkerPool(
    const vk::Device device,
    const vma::Allocator allocator,
    StagingBuffer &stagingBuffer,
    TransferThread &transferThread,
    const uint32_t threadCount = std::thread::hardware_concurrency() - 2
  ) : m_device(device), m_allocator(allocator),
      m_stagingBuffer(stagingBuffer), m_transferThread(transferThread) {
    ZoneScoped;
    for (int i = 0; i < threadCount; ++i) {
      m_threads.emplace_back([this, i] { threadLoop(i); });
    }
  }

  ~TextureWorkerPool() {
    m_stop = true;
    for (size_t i = 0; i < m_threads.size(); ++i)
      m_queue.enqueue(TextureLoadJob{});
    for (auto &tread: m_threads) {
      if (tread.joinable())
        tread.join();
    }
  }

  TextureWorkerPool(const TextureWorkerPool &) = delete;

  TextureWorkerPool &operator=(const TextureWorkerPool &) = delete;

  /**
   * Enqueue a texture loading job
   * @param job texture loading job
   */
  void pushJob(const TextureLoadJob &job) {
    ZoneScoped;
    m_queue.enqueue(job);
  }

  /**
   * Try to dequeue a finished texture load job
   * @param done Out param for finished job
   * @return result of dequeue
   */
  bool tryDequeueDone(TextureLoadDone &done) {
    return m_doneQueue.try_dequeue(done);
  }

private:
  vk::Device m_device = nullptr;
  vma::Allocator m_allocator = nullptr;
  StagingBuffer &m_stagingBuffer;
  TransferThread &m_transferThread;

  std::atomic_bool m_stop;
  std::vector<std::thread> m_threads = {};
  moodycamel::BlockingConcurrentQueue<TextureLoadJob> m_queue;
  moodycamel::ConcurrentQueue<TextureLoadDone> m_doneQueue;

  static void checkResult(const ktx_error_code_e result, const char *what) {
    if (result != KTX_SUCCESS) {
      throw std::runtime_error(std::format("ktx func {} fails with error ({})", what, static_cast<int>(result)));
    }
  }

  void threadLoop(const uint32_t threadIdx) {
    const auto threadName = std::format("Texture Worker {}", threadIdx);
    tracy::SetThreadNameWithHint(threadName.c_str(), UINT8_MAX);
    spdlog::mdc::put("", threadName);
    while (true) {
      TextureLoadJob job{};
      m_queue.wait_dequeue(job);
      if (m_stop.load()) {
        break;
      } {
        ZoneScoped;
        if (!job.filepath.has_extension())
          throw std::invalid_argument("Job filepath must be contains file extension");

        if (job.filepath.extension() == ".ktx" || job.filepath.extension() == ".ktx2") {
          auto texture = loadKtxTexture(job);
          if (texture == nullptr) continue;
          m_doneQueue.enqueue({
            .job = job,
            .texture = std::move(texture)
          });
        } else {
          auto texture = loadGenericTexture(job);
          if (texture == nullptr) continue;
          m_doneQueue.enqueue({
            .job = job,
            .texture = std::move(texture)
          });
        }
      }
    }
  }

  std::unique_ptr<Texture> loadGenericTexture(const TextureLoadJob &job) {
    ZoneScoped;
    int width, height, channels;
    stbi_uc *origPixels; {
      ZoneScopedN("Texture Loading");
      origPixels = stbi_load(
        job.filepath.string().c_str(),
        &width,
        &height,
        &channels,
        4
      );
      if (!origPixels) {
        spdlog::error("Failed to load generic texture image: " + job.filepath.string());
        return nullptr;
      }
    }

    const vk::DeviceSize targetSize = width * height * 4;
    auto mipLevels = static_cast<uint32_t>(
      std::floor(std::log2(std::max(width, height))) + 1
    );

    auto texture = std::make_unique<Texture>(
      m_device, m_allocator,
      width, height, mipLevels,
      vk::Format::eR8G8B8A8Unorm,
      vk::SampleCountFlagBits::e1,
      vk::ImageAspectFlagBits::eColor,
      vk::ImageUsageFlagBits::eSampled
      | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
      true,
      job.filepath.string()
    );

    auto alloc = m_stagingBuffer.allocateBlocking(targetSize); {
      ZoneScopedN("Staging texture data copy");

      if (channels != 4) {
        spdlog::error("Texture {} have {} channels, force load 4", job.filepath.c_str(), channels);
      }
      memcpy(alloc.mapped, origPixels, targetSize);
    } {
      ZoneScopedN("Free stbi ptr");
      stbi_image_free(origPixels);
    }

    const TextureUploadJob copyJob{
      .allocation = alloc,
      .dstImage = texture->getImage(),
      .subresourceRange = vk::ImageSubresourceRange(
        vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1),
      .regions = {
        vk::BufferImageCopy(
          alloc.offset, 0, 0,
          vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
          vk::Offset3D(0, 0, 0),
          vk::Extent3D(width, height, 1))
      },
      .doGenerateMipmaps = mipLevels > 1
    };
    m_transferThread.pushJob(copyJob);

    return texture;
  }

  std::unique_ptr<Texture> loadKtxTexture(const TextureLoadJob &job) {
    ZoneScoped;
    ktxTexture2 *kTexture;
    try {
      {
        ZoneScopedN("Load KTX2 Texture");
        const auto result = ktxTexture2_CreateFromNamedFile(
          job.filepath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);

        if (result != KTX_SUCCESS) {
          throw std::runtime_error("Failed to load KTX texture: " + job.filepath.string());
        }
      }

      if (ktxTexture2_NeedsTranscoding(kTexture)) {
        ZoneScopedN("Transcoding");
        checkResult(ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0),
                    "KTX Transcode");
      }

      auto width = kTexture->baseWidth;
      auto height = kTexture->baseHeight;
      auto mipLevels = kTexture->numLevels;
      if (kTexture->isArray || kTexture->isCubemap || kTexture->numDimensions > 2) {
        spdlog::warn("KTX texture {} it's not plain one, we cannot load it", job.filepath.string());
        return nullptr;
      }
      auto texture = std::make_unique<Texture>(
        m_device, m_allocator,
        width, height, mipLevels,
        static_cast<vk::Format>(kTexture->vkFormat),
        vk::SampleCountFlagBits::e1,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageUsageFlagBits::eSampled
        | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
        false,
        job.filepath.string()
      );

      ktx_size_t mipZeroOffset, mipLastOffset;
      checkResult(ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &mipZeroOffset),
                  "KTX GetImageOffset(mip 0)");
      checkResult(ktxTexture_GetImageOffset(ktxTexture(kTexture), mipLevels - 1, 0, 0, &mipLastOffset),
                  "KTX GetImageOffset(mip last)");
      assert(mipLastOffset >= mipZeroOffset);
      const auto mipLastSize = ktxTexture_GetImageSize(ktxTexture(kTexture), mipLevels - 1);
      const auto totalSize = (mipLastOffset + mipLastSize) - mipZeroOffset;
      const auto alloc = m_stagingBuffer.allocateBlocking(totalSize); {
        ZoneScopedN("Staging texture data copy");
        memcpy(alloc.mapped, kTexture->pData + mipZeroOffset, totalSize);
      }

      std::vector<vk::BufferImageCopy> regions;
      regions.reserve(mipLevels); {
        ZoneScopedN("Prepare mipLevels regions");
        uint32_t w = width, h = height;
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
          ktx_size_t currentMipOffset;
          checkResult(ktxTexture_GetImageOffset(ktxTexture(kTexture), mip, 0, 0, &currentMipOffset),
                      "KTX GetImageOffset regions buildup(mip)");

          regions.emplace_back(
            alloc.offset + (currentMipOffset - mipZeroOffset), 0, 0,
            vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip, 0, 1),
            vk::Offset3D(0, 0, 0),
            vk::Extent3D(w, h, 1)
          );

          w = std::max(1u, w >> 1);
          h = std::max(1u, h >> 1);
        }
      }

      const TextureUploadJob copyJob{
        .allocation = alloc,
        .dstImage = texture->getImage(),
        .subresourceRange = vk::ImageSubresourceRange(
          vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1),
        .regions = regions
      };
      m_transferThread.pushJob(copyJob);

      ktxTexture_Destroy(ktxTexture(kTexture));
      return texture;
    } catch (...) {
      spdlog::error("Failed to load KTX texture {}", job.filepath.string());

      if (kTexture != nullptr)
        ktxTexture_Destroy(ktxTexture(kTexture));
      return nullptr;
    }
  }
};

#endif //TEXTUREWORKERSPOOL_H
