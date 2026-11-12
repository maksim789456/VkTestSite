if(NOT DEFINED ENV{VULKAN_SDK} OR "$ENV{VULKAN_SDK}" STREQUAL "")
    message(FATAL_ERROR
            "VULKAN_SDK is not set. Please set VULKAN_SDK before running CMake."
    )
endif()

set(VULKAN_SDK "$ENV{VULKAN_SDK}")

find_program(Slang_SLANGC_EXECUTABLE
        NAMES slangc
        HINTS
        "${VULKAN_SDK}/bin"
        "${VULKAN_SDK}/Bin"
        NO_DEFAULT_PATH
)

if(NOT Slang_SLANGC_EXECUTABLE)
    message(FATAL_ERROR
            "Could not find slangc in Vulkan SDK:\n"
            "  ${VULKAN_SDK}/bin\n"
            "  ${VULKAN_SDK}/Bin"
    )
endif()

message(STATUS "Vulkan SDK: ${VULKAN_SDK}")
message(STATUS "Slang compiler: ${Slang_SLANGC_EXECUTABLE}")