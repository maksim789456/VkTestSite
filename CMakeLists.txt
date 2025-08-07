cmake_minimum_required(VERSION 3.27)

include(CMake/Vcpkg.cmake)
set(VCPKG_TARGET_TRIPLET x64-windows)

project(VkTestSite)

set(CMAKE_CXX_STANDARD 23)

add_executable(VkTestSite src/main.cpp
        src/VkTestSiteApp.cpp
        src/VkTestSiteApp.h
        src/utils.cpp
        src/QueueFamilyIndices.cpp
        src/Swapchain.cpp
        src/Swapchain.h
        src/ShaderModule.cpp
        src/ShaderModule.h
        src/Vertex.h
        src/DescriptorSet.cpp
        src/DescriptorSet.h
        src/DescriptorPool.h
        src/ImGUIStyle.h
        src/Ubo.h
        src/Ubo.tpp
        src/VmaUsage.cpp
        src/BufferUtils.cpp)

find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
target_link_libraries(VkTestSite PRIVATE
        glfw
        Vulkan::Headers
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        Tracy::TracyClient
        unofficial::shaderc::shaderc
        imgui::imgui
)

option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)

add_definitions(-DVALIDATION_ENABLED=ON)