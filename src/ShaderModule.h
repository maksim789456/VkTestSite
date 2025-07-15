#pragma once

#ifndef SHADERMODULE_H
#define SHADERMODULE_H

#include <spirv-reflect/spirv_reflect.h>
#include <vulkan/vulkan.hpp>
#include <fstream>
#include <tracy/Tracy.hpp>

class ShaderModule {
public:
  vk::PipelineShaderStageCreateInfo vertexPipelineInfo;
  vk::PipelineShaderStageCreateInfo fragmentPipelineInfo;

  ShaderModule() = default;
  void load(const std::string &path);
  void compile(const vk::Device &device);
  void destroy(const vk::Device &device) const;

private:
  std::vector<char> spv;
  vk::ShaderModule module;
};


#endif
