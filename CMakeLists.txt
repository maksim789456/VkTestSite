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
        src/Model.cpp
        src/Model.h
        src/Mesh.h
        src/Mesh.tpp
        src/BufferUtils.cpp
        src/Camera.h
        src/Texture.h
        src/texture.cpp
        src/TextureManager.cpp
        src/TextureManager.h
        src/Pipeline.cpp
        src/Pipeline.h
        src/Light.h
)

find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(tinyfiledialogs CONFIG REQUIRED)
find_package(Stb REQUIRED)
target_include_directories(VkTestSite PRIVATE
        ${Stb_INCLUDE_DIR}
)
target_link_libraries(VkTestSite PRIVATE
        glfw
        Vulkan::Headers
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        Tracy::TracyClient
        unofficial::spirv-reflect
        imgui::imgui
        assimp::assimp
        tinyfiledialogs::tinyfiledialogs
)
target_compile_definitions(VkTestSite PRIVATE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)

add_definitions(-DVALIDATION_ENABLED=ON)