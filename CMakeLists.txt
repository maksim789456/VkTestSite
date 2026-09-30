cmake_minimum_required(VERSION 3.27)

include(CMake/Vcpkg.cmake)
set(VCPKG_TARGET_TRIPLET x64-linux)
set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/vcpkg-overlays/ports")

project(VkTestSite VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 23)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

include(CMake/Version.cmake)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
)

add_executable(VkTestSite ${SRC})
target_include_directories(VkTestSite PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_include_directories(VkTestSite PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

include(CMake/Dependencies.cmake)

target_compile_definitions(VkTestSite PRIVATE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

option(TRACY_ENABLE "" ON)
option(TRACY_ON_DEMAND "" ON)

add_definitions(-DVALIDATION_ENABLED=ON)