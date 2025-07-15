#include "ShaderModule.h"

#include <iostream>

void ShaderModule::load(const std::string &path) {
  ZoneScoped;
  auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Failed to open shader source file" << std::endl;
    abort();
  }
  auto fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> spv(fileSize);
  file.seekg(0);
  file.read(spv.data(), fileSize);

  this->spv = spv;
  file.close();
}

void ShaderModule::compile(
  const vk::Device &device
) {
  ZoneScoped;
  //const auto spvModule = spv_reflect::ShaderModule(spv);
  //spvModule.EnumerateDescriptorSets(nullptr, nullptr);

  const auto info = vk::ShaderModuleCreateInfo({}, spv.size(), reinterpret_cast<const uint32_t*>(spv.data()));
  module = device.createShaderModule(info);
  vertexPipelineInfo = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, module, "vertexMain");
  fragmentPipelineInfo = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, module,
                                                           "fragmentMain");
}

void ShaderModule::destroy(const vk::Device &device) const {
  device.destroyShaderModule(module);
}
