#pragma once

#ifndef SHADERMODULE_H
#define SHADERMODULE_H

#include <spirv-reflect/spirv_reflect.h>
#include <vulkan/vulkan.hpp>
#include <tracy/Tracy.hpp>

#include <fstream>
#include <iostream>
#include <filesystem>

#include "DescriptorSet.h"
#include "utils.cpp"

#define SPV_WORD sizeof(uint32_t)
struct DescriptorSetLayoutData {
  uint32_t setNumber;
  vk::DescriptorSetLayoutCreateInfo createInfo;
  std::vector<vk::DescriptorSetLayoutBinding> bindings;
};

class ShaderModule {
public:
  vk::PipelineShaderStageCreateInfo vertexPipelineInfo;
  vk::PipelineShaderStageCreateInfo fragmentPipelineInfo;
  vk::PipelineShaderStageCreateInfo computePipelineInfo;

  ShaderModule() = default;
  void load(const vk::Device &device, const std::string &path);

  void reflectDS();
  void reflectPS(const char* ep, vk::ShaderStageFlags stage);

  void reflect();
  [[nodiscard]] bool isCompute() const {return static_cast<bool>(m_stageFlags & vk::ShaderStageFlagBits::eCompute);}

private:
  void loadSpv(const std::string &path);

  std::string m_name;
  std::vector<uint32_t> m_spv;
  vk::UniqueShaderModule m_module;
  vk::ShaderStageFlags m_stageFlags;
  std::unique_ptr<spv_reflect::ShaderModule> m_spvReflectModule;
  std::vector<DescriptorLayout> m_layouts = {};
  std::vector<vk::PushConstantRange> m_pushConstantRanges = {};
};


#endif
