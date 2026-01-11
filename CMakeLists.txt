cmake_minimum_required(VERSION 3.27)

include(CMake/Vcpkg.cmake)
set(VCPKG_TARGET_TRIPLET x64-linux)
set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/vcpkg-overlays/ports")

project(VkTestSite)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
)

add_executable(VkTestSite ${SRC})
target_include_directories(VkTestSite PRIVATE "${CMAKE_SOURCE_DIR}/src")

include_directories(lib/OpenXR-Hpp/include)

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
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-concurrentqueue CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(OpenXR CONFIG REQUIRED)
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
        spdlog::spdlog
        fmt::fmt
        unofficial::concurrentqueue::concurrentqueue
        KTX::ktx
        OpenXR::openxr_loader
)
target_compile_definitions(VkTestSite PRIVATE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)

add_definitions(-DVALIDATION_ENABLED=ON)