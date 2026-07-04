#include "ShaderModule.h"

void ShaderModule::load(
  const vk::Device &device,
  const std::string &path
) {
  ZoneScoped;
  m_name = std::filesystem::path(path).filename();
  spdlog::info("Loading shader {}", m_name);
  loadSpv(path);

  const auto info = vk::ShaderModuleCreateInfo({}, m_spv.size() * sizeof(uint32_t), m_spv.data());
  m_module = device.createShaderModuleUnique(info);
  setObjectName(device, m_module.get(), std::format("Shader {}", m_name));
}

void ShaderModule::loadSpv(const std::string &path) {
  ZoneScoped;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (file.fail() || !file.is_open()) {
    spdlog::error("Failed to open shader file: {}", path);
    abort();
  }

  const auto fileSize = static_cast<uint32_t>(file.tellg());
  if (fileSize <= 0 || (fileSize % SPV_WORD) != 0) {
    spdlog::error("Invalid shader file size: {}", path);
    abort();
  }
  m_spv.resize(fileSize / SPV_WORD);

  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char *>(m_spv.data()), fileSize)) {
    spdlog::error("Failed to read shader file: {}", path);
    abort();
  }

  file.close();
}

void ShaderModule::reflectDS(const char *ep, vk::ShaderStageFlags stage) {
  ZoneScoped;
  uint32_t count = 0;
  auto result = m_spvReflectModule->EnumerateEntryPointDescriptorBindings(ep, &count, nullptr);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectDescriptorBinding *> bindings(count);
  result = m_spvReflectModule->EnumerateEntryPointDescriptorBindings(ep, &count, bindings.data());
  assert(result == SPV_REFLECT_RESULT_SUCCESS);


  for (const auto *reflBinding: bindings) {
    DescriptorKey key{
      reflBinding->set,
      reflBinding->binding
    };

    auto [it, inserted] = m_layouts
        .try_emplace(key, DescriptorLayout{});
    auto &layout = it->second;

    layout.shaderBinding = reflBinding->binding;
    layout.type = static_cast<vk::DescriptorType>(reflBinding->descriptor_type);
    layout.count = 1;
    for (uint32_t iDim = 0; iDim < reflBinding->array.dims_count; ++iDim) {
      layout.count *= reflBinding->array.dims[iDim];
    }
    if (reflBinding->array.dims_count > 0
        && reflBinding->array.dims[0] == SPV_REFLECT_ARRAY_DIM_RUNTIME) {
      layout.count = 1;
      layout.bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound
                            | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    }
    layout.stage |= stage;
  }
}

void ShaderModule::reflectPS(const char *ep, vk::ShaderStageFlags stage) {
  ZoneScoped;
  uint32_t count = 0;
  auto result = m_spvReflectModule->EnumerateEntryPointPushConstantBlocks(ep, &count, nullptr);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectBlockVariable *> pcs(count);
  result = m_spvReflectModule->EnumerateEntryPointPushConstantBlocks(ep, &count, pcs.data());
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  for (auto *pc: pcs) {
    spdlog::info("Push const block: {} on {}", pc->name ? pc->name : "<unnamed>", ep);
    for (uint32_t i = 0; i < pc->member_count; i++) {
      auto &m = pc->members[i];
      spdlog::info("\tmember: {}, offset: {}, size: {}", m.name, m.offset, m.size);
      m_pushConstantRanges.emplace_back(stage, m.offset, m.size);
    }
  }
}

void ShaderModule::reflect() {
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
      reflectDS(ep->name, m_stageFlags);
      return;
    }

    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
      vertexPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eVertex, m_module.get(), ep->name);
      m_stageFlags |= vk::ShaderStageFlagBits::eVertex;
      spdlog::info("\t({}, ep: {}) -> vertex", i, ep->name);
      reflectPS(ep->name, vk::ShaderStageFlagBits::eVertex);
      reflectDS(ep->name, vk::ShaderStageFlagBits::eVertex);
    }
    if (ep->shader_stage & SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT) {
      fragmentPipelineInfo = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eFragment, m_module.get(), ep->name);
      m_stageFlags |= vk::ShaderStageFlagBits::eFragment;
      spdlog::info("\t({}, ep: {}) -> fragment", i, ep->name);
      reflectPS(ep->name, vk::ShaderStageFlagBits::eFragment);
      reflectDS(ep->name, vk::ShaderStageFlagBits::eFragment);
    }
  }

  for (const auto &layout: m_layouts) {
    spdlog::info("\t[{}, {}] count: {}, type: {}, stage: {}",
                 layout.first.binding, layout.first.set, layout.second.count, vk::to_string(layout.second.type),
                 vk::to_string(layout.second.stage));
  }
}

vk::PipelineLayout ShaderModule::buildLayout(
  const vk::Device &device
) {
  auto layoutBindingsFlags = std::vector<vk::DescriptorBindingFlags>();
  auto layoutBindingsAllFlags = vk::DescriptorBindingFlags{};
  auto layoutBindings = std::vector<vk::DescriptorSetLayoutBinding>();
  for (const auto &layout: m_layouts) {
    layoutBindingsAllFlags |= layout.second.bindingFlags;
    layoutBindingsFlags.emplace_back(layout.second.bindingFlags);
    layoutBindings.emplace_back(
      layout.second.shaderBinding, layout.second.type, layout.second.count, layout.second.stage
    );
  }

  auto dslFlags = vk::DescriptorSetLayoutCreateFlags();
  if (layoutBindingsAllFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) {
    dslFlags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
  }

  auto dslInfo = vk::DescriptorSetLayoutCreateInfo(dslFlags, layoutBindings);
  const auto flagsInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo(layoutBindingsFlags);
  dslInfo.pNext = &flagsInfo;
  m_descriptorSetLayout = device.createDescriptorSetLayout(dslInfo);
  setObjectName(device, m_descriptorSetLayout, std::format("{} layout", m_name));

  const auto plInfo = vk::PipelineLayoutCreateInfo({}, m_descriptorSetLayout, m_pushConstantRanges);
  m_pipelineLayout = device.createPipelineLayout(plInfo);
  setObjectName(device, m_pipelineLayout, std::format("{} pipeline layout", m_name));

  return m_pipelineLayout;
}
