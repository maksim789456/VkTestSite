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

void ShaderModule::reflectDS() {
  ZoneScoped;
  uint32_t count = 0;
  auto result = m_spvReflectModule->EnumerateDescriptorSets(&count, nullptr);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectDescriptorSet *> sets(count);
  result = m_spvReflectModule->EnumerateDescriptorSets(&count, sets.data());
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  spdlog::info("contains {} descriptor sets", sets.size());
  if (sets.size() > 1) {
    spdlog::warn("WARN: Shader {} use more that one DS, reflect only covers first DS!", m_name);
  }
  const SpvReflectDescriptorSet &reflSet = *sets[0];

  m_layouts.resize(reflSet.binding_count);
  for (int iBinding = 0; iBinding < reflSet.binding_count; ++iBinding) {
    const auto &reflBinding = *reflSet.bindings[iBinding];
    auto &layout = m_layouts[iBinding];
    layout.shaderBinding = reflBinding.binding;
    layout.type = static_cast<vk::DescriptorType>(reflBinding.descriptor_type);
    layout.count = 1;
    for (uint32_t iDim = 0; iDim < reflBinding.array.dims_count; ++iDim) {
      layout.count *= reflBinding.array.dims[iDim];
    }
    layout.stage = m_stageFlags;
  }

  spdlog::info("Descriptor set 0, num {}, {} bindings", reflSet.set, reflSet.binding_count);
  for (const auto & layout : m_layouts) {
    spdlog::info("\t[{}] count: {}, type: {}, stage: {}",
                 layout.shaderBinding, layout.count, vk::to_string(layout.type), vk::to_string(layout.stage));
  }
}

void ShaderModule::reflectPS(const char* ep, vk::ShaderStageFlags stage)
{
  ZoneScoped;
  uint32_t count = 0;
  auto result = m_spvReflectModule->EnumerateEntryPointPushConstantBlocks(ep, &count, nullptr);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectBlockVariable *> pcs(count);
  result = m_spvReflectModule->EnumerateEntryPointPushConstantBlocks(ep, &count, pcs.data());
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  for (auto* pc : pcs)
  {
    spdlog::info("Push const block: {} on {}", pc->name ? pc->name : "<unnamed>", ep);
    for (uint32_t i = 0; i < pc->member_count; i++)
    {
      auto& m = pc->members[i];
      spdlog::info("\tmember: {}, offset: {}, size: {}", m.name, m.offset, m.size);
      m_pushConstantRanges.emplace_back(stage, m.offset, m.size);
    }
  }
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
      m_stageFlags = vk::ShaderStageFlagBits::eCompute;
      spdlog::info("\t({}, ep: {}) -> compute", i, ep->name);
      reflectPS(ep->name, m_stageFlags);
      return;
    }

    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
      vertexPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eVertex, m_module.get(), ep->name);
      m_stageFlags |= vk::ShaderStageFlagBits::eVertex;
      spdlog::info("\t({}, ep: {}) -> vertex", i, ep->name);
      reflectPS(ep->name, vk::ShaderStageFlagBits::eVertex);
    }
    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT) {
      fragmentPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eFragment, m_module.get(), ep->name);
      m_stageFlags |= vk::ShaderStageFlagBits::eFragment;
      spdlog::info("\t({}, ep: {}) -> fragment", i, ep->name);
      reflectPS(ep->name, vk::ShaderStageFlagBits::eFragment);
    }
  }

  reflectDS();
}
