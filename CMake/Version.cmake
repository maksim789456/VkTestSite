find_package(Git REQUIRED)

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