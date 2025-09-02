#include "ShaderModule.h"

void ShaderModule::load(
  const vk::Device &device,
  const std::string &path
) {
  ZoneScoped;
  auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
  if (file.fail() || !file.is_open()) {
    std::cerr << "Failed to open shader source file" << std::endl;
    abort();
  }

  const auto fileSize = static_cast<uint32_t>(file.tellg());
  const auto bufferSize = fileSize / sizeof(uint32_t);
  std::vector<uint32_t> spv(bufferSize);

  file.seekg(0);
  auto *fileData = reinterpret_cast<char *>(spv.data());
  file.read(fileData, fileSize);
  file.close();

  this->m_spv = spv;
  const auto info = vk::ShaderModuleCreateInfo({}, spv.size() * sizeof(uint32_t), spv.data());
  m_module = device.createShaderModuleUnique(info);
}

void ShaderModule::reflect(
  const vk::Device &device
) {
  ZoneScoped;
  m_spvReflectModule = std::make_unique<spv_reflect::ShaderModule>(m_spv);
  if (m_spvReflectModule->GetResult() != SPV_REFLECT_RESULT_SUCCESS) {
    std::cerr << "Failed to reflect shader module" << std::endl;
    abort();
  }

  for (int i = 0; i < m_spvReflectModule->GetEntryPointCount(); ++i) {
    const auto ep = spvReflectGetEntryPoint(
      &m_spvReflectModule->GetShaderModule(),
      m_spvReflectModule->GetEntryPointName(i)
    );

    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
      vertexPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eVertex, m_module.get(), ep->name);
    }
    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT) {
      fragmentPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eFragment, m_module.get(), ep->name);
    }
  }
}
