#include "ShaderModule.h"

#include <iostream>

ShaderModule::ShaderModule(): stage() {
}

ShaderModule::ShaderModule(const vk::ShaderStageFlagBits stage) {
  this->stage = stage;
}

void ShaderModule::load(const std::string &path) {
  ZoneScoped;
  auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Failed to open shader source file" << std::endl;
    abort();
  }
  auto fileSize = file.tellg();
  file.seekg(0, std::ios::beg);
  std::string source(fileSize, '\0');
  file.read(&source[0], fileSize);

  this->source = source;
  file.close();
}

void ShaderModule::compile(
  const vk::Device &device
) {
  ZoneScoped;
  const shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);

  shaderc_shader_kind kind = {};
  if (stage == vk::ShaderStageFlagBits::eVertex) {
    kind = shaderc_glsl_vertex_shader;
  } else if (stage == vk::ShaderStageFlagBits::eFragment) {
    kind = shaderc_glsl_fragment_shader;
  } else {
    std::cerr << "Unsupported shader stage: " << to_string(stage) << std::endl;
    abort();
  }

  const auto shaderModule =
      compiler.CompileGlslToSpv(source, kind, "shader", options);
  if (shaderModule.GetCompilationStatus() != shaderc_compilation_status_success) {
    std::cerr << "Failed to compile shader: " << shaderModule.GetErrorMessage() << std::endl;
    abort();
  }
  const auto shaderCode = std::vector<uint32_t>{shaderModule.cbegin(), shaderModule.cend()};
  const auto size = std::distance(shaderCode.cbegin(), shaderCode.cend());
  const auto info = vk::ShaderModuleCreateInfo({}, size * sizeof(uint32_t), shaderCode.data());
  module = device.createShaderModule(info);
  pipeline_info = vk::PipelineShaderStageCreateInfo({}, stage, module, "main");
}

void ShaderModule::destroy(const vk::Device &device) const {
  device.destroyShaderModule(module);
}
