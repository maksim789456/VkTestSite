cmake_minimum_required(VERSION 3.27)

include(CMake/Vcpkg.cmake)
set(VCPKG_TARGET_TRIPLET x64-linux)
set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/vcpkg-overlays/ports")

project(VkTestSite VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 23)

find_package(Git REQUIRED)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
        COMMAND ${GIT_EXECUTABLE} branch --show-current
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(VKTS_VERSION_STRING "${PROJECT_VERSION}-${GIT_BRANCH}.${GIT_HASH}")

configure_file(src/core/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
)

add_executable(VkTestSite ${SRC})
target_include_directories(VkTestSite PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_include_directories(VkTestSite PRIVATE
        ${CMAKE_BINARY_DIR}/generated
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
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
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
        concurrentqueue::concurrentqueue
        KTX::ktx
)
target_compile_definitions(VkTestSite PRIVATE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)

add_definitions(-DVALIDATION_ENABLED=ON)