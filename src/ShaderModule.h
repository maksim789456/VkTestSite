#pragma once

#ifndef SHADERMODULE_H
#define SHADERMODULE_H

#include <spirv-reflect/spirv_reflect.h>
#include <vulkan/vulkan.hpp>
#include <tracy/Tracy.hpp>

#include <fstream>
#include <iostream>

class ShaderModule {
public:
  vk::PipelineShaderStageCreateInfo vertexPipelineInfo;
  vk::PipelineShaderStageCreateInfo fragmentPipelineInfo;

  ShaderModule() = default;
  void load(const vk::Device &device, const std::string &path);
  void reflect(const vk::Device &device);

private:
  std::vector<uint32_t> m_spv;
  vk::UniqueShaderModule m_module;
  std::unique_ptr<spv_reflect::ShaderModule> m_spvReflectModule;
};


#endif
