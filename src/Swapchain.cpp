#include "Swapchain.h"

vk::SurfaceFormatKHR get_swapchain_surface_format(
  const vk::SurfaceKHR &surface,
  const vk::PhysicalDevice &physical_device
) {
  ZoneScoped;
  const auto formats = physical_device.getSurfaceFormatsKHR(surface);

  for (const auto format: formats) {
    if (format.format == vk::Format::eB8G8R8A8Unorm
        && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      return format;
    }
  }

  return formats[0];
}

vk::Extent2D get_swapchain_extent(
  GLFWwindow *window,
  const vk::SurfaceKHR &surface,
  const vk::PhysicalDevice &physical_device
) {
  ZoneScoped;
  auto capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);

  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  auto extent = vk::Extent2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  extent.width = std::clamp(
    extent.width,
    capabilities.minImageExtent.width,
    capabilities.maxImageExtent.width
  );
  extent.height = std::clamp(
    extent.height,
    capabilities.minImageExtent.height,
    capabilities.maxImageExtent.height
  );
  return extent;
}

std::vector<vk::ImageView> create_swapchain_image_views(
  const vk::Device &device,
  const std::vector<vk::Image> &swapchainImages,
  vk::Format swapchainImageFormat
) {
  ZoneScoped;
  std::vector<vk::ImageView> imageViews;
  imageViews.reserve(swapchainImages.size());

  for (const auto &image: swapchainImages) {
    vk::ImageSubresourceRange subresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::ImageViewCreateInfo info({}, image, vk::ImageViewType::e2D, swapchainImageFormat, {}, subresourceRange);
    imageViews.push_back(device.createImageView(info));
  }

  return imageViews;
}

Swapchain::Swapchain(): format() {
}

Swapchain::Swapchain(
  const vk::SurfaceKHR &surface,
  const vk::Device &device,
  const vk::PhysicalDevice &physical_device,
  GLFWwindow *window
) {
  ZoneScoped;
  auto indices = QueueFamilyIndices(surface, physical_device);
  auto format_khr = get_swapchain_surface_format(surface, physical_device);
  this->format = format_khr.format;
  auto present_mode = vk::PresentModeKHR::eFifo;
  this->extent = get_swapchain_extent(window, surface, physical_device);

  auto capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);

  auto image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  std::vector<uint32_t> queue_family_indices{};
  vk::SharingMode sharing_mode{};
  if (indices.graphics != indices.present) {
    queue_family_indices.push_back(indices.graphics);
    queue_family_indices.push_back(indices.present);
    sharing_mode = vk::SharingMode::eConcurrent;
  } else {
    sharing_mode = vk::SharingMode::eExclusive;
  }

  vk::SwapchainCreateInfoKHR info = vk::SwapchainCreateInfoKHR{}
      .setSurface(surface)
      .setMinImageCount(image_count)
      .setImageFormat(format_khr.format)
      .setImageColorSpace(format_khr.colorSpace)
      .setImageExtent(extent)
      .setImageArrayLayers(1)
      .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
      .setImageSharingMode(sharing_mode)
      .setQueueFamilyIndices(queue_family_indices)
      .setPreTransform(capabilities.currentTransform)
      .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
      .setPresentMode(present_mode)
      .setClipped(true)
      .setOldSwapchain(nullptr);

  swapchain = device.createSwapchainKHR(info);
  images = device.getSwapchainImagesKHR(swapchain);
  imageViews = create_swapchain_image_views(device, images, format);
}

void Swapchain::cmdSetViewport(const vk::CommandBuffer cmdBuffer) const {
  const auto viewport = vk::Viewport(0, 0, extent.width, extent.height, 0, 1);
  cmdBuffer.setViewport(0, viewport);
}

void Swapchain::cmdSetScissor(const vk::CommandBuffer cmdBuffer) const {
  const auto scissorRect = vk::Rect2D(vk::Offset2D(), extent);
  cmdBuffer.setScissor(0, scissorRect);
}

void Swapchain::destroy(const vk::Device &device) {
  for (auto image_view: imageViews) {
    device.destroyImageView(image_view);
  }
  device.destroySwapchainKHR(swapchain);
}
