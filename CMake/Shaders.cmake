include(CMake/CompilerSlangShader.cmake)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res/shaders")
file(GLOB_RECURSE SHADER_EP_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.ep.slang")
file(GLOB_RECURSE SHADER_CMP_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.cmp.slang")

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/_autogen")

set(SHADER_CAPABILITIES
        spvShaderNonUniformEXT
        SPV_GOOGLE_user_type
        spvDerivativeControl
        spvImageQuery
        spvImageGatherExtended
        spvSparseResidency
        spvMinLod
        spvFragmentFullyCoveredEXT
)

compile_slang(
        "${SHADER_EP_FILES}"
        "${SHADER_DIR}"
        SPVS_VAR COMPILED_SLANG_SHADERS
        CAPABILITIES ${SHADER_CAPABILITIES}
        EXTRA_FLAGS
            -I "${SHADER_DIR}"
            -entry vertexMain
            -entry fragmentMain
            -DDEBUG=1
)

compile_slang(
        "${SHADER_CMP_FILES}"
        "${SHADER_DIR}"
        SPVS_VAR COMPILED_SLANG_SHADERS
        CAPABILITIES ${SHADER_CAPABILITIES}
        EXTRA_FLAGS
            -I "${SHADER_DIR}"
            -entry cmpMain
            -DDEBUG=1
)

source_group("Shaders" FILES ${SHADER_EP_FILES} ${SHADER_H_FILES})
source_group("Shaders/Compiled" FILES ${COMPILED_SLANG_SHADERS})

target_sources(${PROJECT_NAME} PRIVATE ${COMPILED_SLANG_SHADERS})