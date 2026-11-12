#ifndef NULLTEXTURE_H
#define NULLTEXTURE_H
#include "Texture.h"

/**
 * 2x2 texture with black/magenta checker pattern.
 * Used mostly as placeholder
 */
class NullTexture : public Texture {
public:
  NullTexture(
    const vk::Device device,
    const vma::Allocator allocator,
    const vk::Queue graphicsQueue,
    const vk::CommandPool commandPool
  ) : Texture(
    device, allocator, 2, 2, 1, vk::Format::eR8G8B8A8Unorm,
    vk::SampleCountFlagBits::e1,
    vk::ImageAspectFlagBits::eColor,
    vk::ImageUsageFlagBits::eSampled |
    vk::ImageUsageFlagBits::eTransferDst,
    true,
    "Null Texture"
  ) {
    fillTexture(device, allocator, graphicsQueue, commandPool);
  }

private:
  void fillTexture(
    const vk::Device device,
    const vma::Allocator allocator,
    const vk::Queue graphicsQueue,
    const vk::CommandPool commandPool
  ) {
    const std::vector<uint8_t> pixelData = {
      255, 0, 255, 255,
      0, 0, 0, 255,
      0, 0, 0, 255,
      255, 0, 255, 255
    };
    const VkDeviceSize bufferSize = pixelData.size() * sizeof(uint8_t);

    auto [pixelBufferAlloc, pixelBuffer] = createBufferUnique(
      allocator, bufferSize,
      vk::BufferUsageFlagBits::eTransferSrc,
      vma::MemoryUsage::eAuto,
      vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
    );
    fillBuffer(allocator, pixelBufferAlloc.get(), bufferSize, pixelData);

    executeSingleTimeCommands(device, graphicsQueue, commandPool, [&](const vk::CommandBuffer cmd) {
      cmdTransitionImageLayout(
        cmd, getImage(),
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);

      constexpr auto subresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
      constexpr auto region = vk::BufferImageCopy(
        0, 0, 0, subresource,
        vk::Offset3D(0, 0, 0),
        vk::Extent3D(2, 2, 1)
      );
      cmd.copyBufferToImage(pixelBuffer.get(), getImage(), vk::ImageLayout::eTransferDstOptimal, region);
      cmdTransitionImageLayout(
        cmd, getImage(),
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);
    });
  }
};


#endif //NULLTEXTURE_H
