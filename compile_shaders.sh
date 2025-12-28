#!/usr/bin/env zsh

SHADER_DIR="./res/shaders"

if [ -z "$VULKAN_SDK" ]; then
    echo "VULKAN_SDK is not set. Cannot locate slangc."
    exit 1
fi

source $VULKAN_SDK/../setup-env.sh

# Vertex/Fragment shaders: *.ep.slang
while IFS= read -r -d '' src; do
    target="${src}.spv"
    slangc \
        -profile glsl_460 \
        -capability spvShaderNonUniformEXT \
        -target spirv \
        -emit-spirv-directly \
        -force-glsl-scalar-layout \
        -fvk-use-entrypoint-name \
        -I "$SHADER_DIR" \
        -entry vertexMain \
        -entry fragmentMain \
        -g \
        -DDEBUG=1 \
        -o "$target" \
        "$src"
done < <(find "$SHADER_DIR" -type f -name "*.ep.slang" ! -name "*.spv" -print0)

# Compute shaders: *.cmp.slang
while IFS= read -r -d '' src; do
    target="${src}.spv"
    slangc \
        -profile glsl_460 \
        -target spirv \
        -emit-spirv-directly \
        -force-glsl-scalar-layout \
        -fvk-use-entrypoint-name \
        -I "$SHADER_DIR" \
        -entry cmpMain \
        -g \
        -DDEBUG=1 \
        -o "$target" \
        "$src"
done < <(find "$SHADER_DIR" -type f -name "*.cmp.slang" ! -name "*.spv" -print0)
