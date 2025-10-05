#ifndef TEXTUREWORKERSPOOL_H
#define TEXTUREWORKERSPOOL_H

#include <vulkan/vulkan.hpp>
#include "concurrentqueue/blockingconcurrentqueue.h"
#include "concurrentqueue/concurrentqueue.h"
#include <stb_image.h>

#include <ktx.h>
#include <ktxvulkan.h>

#include "StagingBuffer.h"
#include "TransferThread.h"
#include "Texture.h"
#include <filesystem>

struct TextureLoadJob {
  uint32_t texIndex;
  std::filesystem::path filepath;
};

struct TextureLoadDone {
  TextureLoadJob job;
  std::unique_ptr<Texture> texture;
};

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
      m_threads.emplace_back([this, i]() { threadLoop(i); });
    }
  }

  ~TextureWorkerPool() {
    m_stop = true;
    for (auto &tread: m_threads) {
      if (tread.joinable())
        tread.join();
    }
  }

  TextureWorkerPool(const TextureWorkerPool &) = delete;

  TextureWorkerPool &operator=(const TextureWorkerPool &) = delete;

  void pushJob(const TextureLoadJob &job) {
    ZoneScoped;
    m_queue.enqueue(job);
  }

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

  void threadLoop(const uint32_t threadIdx) {
    tracy::SetThreadNameWithHint(std::format("Texture Worker {}", threadIdx).c_str(), UINT8_MAX);
    while (!m_stop) {
      TextureLoadJob job{};
      m_queue.wait_dequeue(job); {
        ZoneScoped;
        if (!job.filepath.has_extension())
          throw std::invalid_argument("Job filepath must be contains file extension");

        if (job.filepath.extension() == ".ktx" || job.filepath.extension() == ".ktx2") {
          auto texture = loadKtxTexture(job);
          m_doneQueue.enqueue({
            .job = job,
            .texture = std::move(texture)
          });
        } else {
          auto texture = loadGenericTexture(job);
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
        0
      );
      if (!origPixels) {
        throw std::runtime_error("Failed to load texture image: " + job.filepath.string());
      }
    }

    const vk::DeviceSize targetSize = width * height * 4;
    const vk::DeviceSize originalSize = width * height * channels;
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
      //memcpy(alloc.mapped, pixels.data(), pixels.size());
      auto *mappedBytes = static_cast<std::uint8_t *>(alloc.mapped);

      if (channels == 4) {
        memcpy(alloc.mapped, origPixels, originalSize);
      } else if (channels == 3) {
        for (int i = 0; i < width * height; ++i) {
          mappedBytes[i * 4 + 0] = origPixels[i * 3 + 0];
          mappedBytes[i * 4 + 1] = origPixels[i * 3 + 1];
          mappedBytes[i * 4 + 2] = origPixels[i * 3 + 2];
          mappedBytes[i * 4 + 3] = 255;
        }
      }
    } {
      ZoneScopedN("Free stbi ptr");
      stbi_image_free(origPixels);
    }

    constexpr auto subresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    const TextureUploadJob copyJob{
      .allocation = alloc,
      .dstImage = texture->getImage(),
      .subresourceRange = vk::ImageSubresourceRange(
        vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1),
      .region = vk::BufferImageCopy(
        alloc.offset, 0, 0, subresource,
        vk::Offset3D(0, 0, 0),
        vk::Extent3D(width, height, 1)),
    };
    m_transferThread.pushJob(copyJob);

    return texture;
  }

  std::unique_ptr<Texture> loadKtxTexture(const TextureLoadJob &job) {
    ZoneScoped;
    ktxTexture2 *kTexture; {
      ZoneScopedN("Load KTX2 Texture");
      auto result = ktxTexture2_CreateFromNamedFile(
        job.filepath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);

      if (result != KTX_SUCCESS) {
        throw std::runtime_error("Failed to load KTX: " + job.filepath.string());
      }
    }
    if (ktxTexture2_NeedsTranscoding(kTexture)) {
      ZoneScopedN("Transcoding");
      auto result = ktxTexture2_TranscodeBasis(
        kTexture,
        KTX_TTF_BC7_RGBA,
        0);
      if (result != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(kTexture));
        throw std::runtime_error("Failed to transcode KTX2 texture");
      }
    }

    auto width = kTexture->baseWidth;
    auto height = kTexture->baseHeight;
    auto mipLevels = kTexture->numLevels;
    auto texture = std::make_unique<Texture>(
      m_device, m_allocator,
      width, height, mipLevels,
      static_cast<vk::Format>(kTexture->vkFormat),
      vk::SampleCountFlagBits::e1,
      vk::ImageAspectFlagBits::eColor,
      vk::ImageUsageFlagBits::eSampled
      | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
      true,
      job.filepath.string()
    );

    ktx_size_t offset;
    auto result = ktxTexture_GetImageOffset(
      ktxTexture(kTexture), 0, 0, 0, &offset
    );
    if (result != KTX_SUCCESS) {
      ktxTexture_Destroy(ktxTexture(kTexture));
      throw std::runtime_error("Failed to get KTX2 image offset");
    }

    size_t size = ktxTexture_GetImageSize(ktxTexture(kTexture), 0);
    auto alloc = m_stagingBuffer.allocateBlocking(size); {
      ZoneScopedN("Staging texture data copy");
      memcpy(alloc.mapped, kTexture->pData + offset, size);
    }

    constexpr auto subresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    const TextureUploadJob copyJob{
      .allocation = alloc,
      .dstImage = texture->getImage(),
      .subresourceRange = vk::ImageSubresourceRange(
        vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1),
      .region = vk::BufferImageCopy(
        alloc.offset, 0, 0, subresource,
        vk::Offset3D(0, 0, 0),
        vk::Extent3D(width, height, 1)),
    };
    m_transferThread.pushJob(copyJob);

    ktxTexture_Destroy(ktxTexture(kTexture));

    return texture;
  }
};

#endif //TEXTUREWORKERSPOOL_H
