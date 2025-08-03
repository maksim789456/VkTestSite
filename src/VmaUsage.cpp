#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1003000
#define VMA_DEBUG_LOG(format, ...) do { \
printf(format, __VA_ARGS__); \
printf("\n"); \
} while(false)
#include "vulkan-memory-allocator-hpp/vk_mem_alloc.hpp"
