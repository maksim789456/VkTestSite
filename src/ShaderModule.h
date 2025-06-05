#pragma once

#ifndef SHADERMODULE_H
#define SHADERMODULE_H

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.hpp>
#include <fstream>
#include <tracy/Tracy.hpp>

class ShaderModule {
public:
  vk::PipelineShaderStageCreateInfo pipeline_info;

  ShaderModule();
  ShaderModule(vk::ShaderStageFlagBits stage);
  void load(const std::string &path);
  void compile(const vk::Device &device);
  void destroy(const vk::Device &device) const;

private:
  vk::ShaderStageFlagBits stage;
  std::string source;
  vk::ShaderModule module;
};


#endif
