#pragma once

#ifndef SHADERMODULE_H
#define SHADERMODULE_H

#include <spirv-reflect/spirv_reflect.h>
#include <vulkan/vulkan.hpp>
#include <tracy/Tracy.hpp>

#include <fstream>
#include <iostream>
#include <filesystem>
#include "utils.cpp"

class ShaderModule {
public:
  vk::PipelineShaderStageCreateInfo vertexPipelineInfo;
  vk::PipelineShaderStageCreateInfo fragmentPipelineInfo;
  vk::PipelineShaderStageCreateInfo computePipelineInfo;

  ShaderModule() = default;
  void load(const vk::Device &device, const std::string &path);
  void reflect(const vk::Device &device);
  [[nodiscard]] bool isCompute() const {return m_isCompute;}

private:
  bool m_isCompute = false;
  std::vector<uint32_t> m_spv;
  vk::UniqueShaderModule m_module;
  std::unique_ptr<spv_reflect::ShaderModule> m_spvReflectModule;
};


#endif
