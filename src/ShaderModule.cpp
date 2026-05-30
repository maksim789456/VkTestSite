#include "ShaderModule.h"

void ShaderModule::load(
  const vk::Device &device,
  const std::string &path
) {
  ZoneScoped;
  m_name = std::filesystem::path(path).filename();
  auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
  if (file.fail() || !file.is_open()) {
    spdlog::error("Failed to open shader source file");
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
  spdlog::info("Loading shader {}", m_name);
  setObjectName(device, m_module.get(), std::format("Shader {}", m_name));
}

void ShaderModule::reflect(
  const vk::Device &device
) {
  ZoneScoped;
  spdlog::info("Reflect shader {}", m_name);
  m_spvReflectModule = std::make_unique<spv_reflect::ShaderModule>(m_spv);
  if (m_spvReflectModule->GetResult() != SPV_REFLECT_RESULT_SUCCESS) {
    spdlog::error("Failed to reflect shader module");
    abort();
  }

  for (int i = 0; i < m_spvReflectModule->GetEntryPointCount(); ++i) {
    const auto ep = spvReflectGetEntryPoint(
      &m_spvReflectModule->GetShaderModule(),
      m_spvReflectModule->GetEntryPointName(i)
    );

    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT) {
      computePipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eCompute, m_module.get(), ep->name);
      m_isCompute = true;
      spdlog::info("\t({}, ep: {}) -> compute", i, ep->name);
      return;
    }

    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
      vertexPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eVertex, m_module.get(), ep->name);
      spdlog::info("\t({}, ep: {}) -> vertex", i, ep->name);
    }
    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT) {
      fragmentPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eFragment, m_module.get(), ep->name);
      spdlog::info("\t({}, ep: {}) -> fragment", i, ep->name);
    }
  }
}
