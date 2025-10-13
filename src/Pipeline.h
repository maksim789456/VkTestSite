#ifndef PIPELINE_H
#define PIPELINE_H

#include <filesystem>

#include "ShaderModule.h"

class PipelineBuilder {
public:
  PipelineBuilder(
    const vk::Device device,
    const vk::RenderPass renderPass,
    const vk::PipelineLayout pipelineLayout,
    const std::string &path,
    const std::string &name = "Pipeline"
  ): m_device(device), m_renderPass(renderPass), m_pipelineLayout(pipelineLayout), m_name(name) {
    m_shaderModule = std::make_unique<ShaderModule>();
    m_shaderModule->load(m_device, path);
    m_shaderModule->reflect(m_device);
  }

  PipelineBuilder &withBindingDescriptions(
    const std::vector<vk::VertexInputBindingDescription> &bindingDescriptions) {
    m_bindingDescriptions = bindingDescriptions;
    return *this;
  }

  PipelineBuilder &withAttributeDescriptions(
    const std::vector<vk::VertexInputAttributeDescription> &attributeDescriptions) {
    m_attributeDescriptions = attributeDescriptions;
    return *this;
  }

  PipelineBuilder &withColorBlendAttachments(
    const std::vector<vk::PipelineColorBlendAttachmentState> &colorBlendAttachments) {
    m_colorBlendAttachments = colorBlendAttachments;
    return *this;
  }

  PipelineBuilder &withCullMode(const vk::CullModeFlagBits cullMode) {
    m_cullMode = cullMode;
    return *this;
  }

  PipelineBuilder &withSubpass(const uint32_t subpass) {
    m_subpass = subpass;
    return *this;
  }

  PipelineBuilder &withMsaa(
    const bool msaaEnabled, const vk::SampleCountFlagBits samples, const float minSample
  ) {
    m_msaaEnabled = msaaEnabled;
    m_msaaSamples = samples;
    m_msaaMinSample = minSample;
    return *this;
  }

  PipelineBuilder &depthStencil(
    const bool testEnabled, const bool writeEnabled, const vk::CompareOp compareOp
  ) {
    m_depthTestEnabled = testEnabled;
    m_depthWriteEnabled = writeEnabled;
    m_depthCompareOp = compareOp;
    return *this;
  }

  static vk::PipelineColorBlendAttachmentState makeDefaultColorAttachmentState() {
    auto colorAttachment = vk::PipelineColorBlendAttachmentState(
      false,
      vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha,
      vk::BlendOp::eAdd,
      vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha,
      vk::BlendOp::eAdd);

    colorAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    return colorAttachment;
  }

  vk::Pipeline buildGraphics();
  vk::Pipeline buildCompute();

private:
  std::string m_name;
  std::vector<vk::VertexInputBindingDescription> m_bindingDescriptions;
  std::vector<vk::VertexInputAttributeDescription> m_attributeDescriptions;
  std::optional<std::vector<vk::PipelineColorBlendAttachmentState> > m_colorBlendAttachments = std::nullopt;
  vk::CullModeFlagBits m_cullMode = vk::CullModeFlagBits::eBack;

  bool m_msaaEnabled = false;
  vk::SampleCountFlagBits m_msaaSamples = vk::SampleCountFlagBits::e1;
  float m_msaaMinSample = 0.0f;

  bool m_depthTestEnabled = true;
  bool m_depthWriteEnabled = true;
  vk::CompareOp m_depthCompareOp = vk::CompareOp::eLess;

  uint32_t m_subpass = 0;

  vk::Device m_device = nullptr;
  vk::RenderPass m_renderPass = nullptr;
  vk::PipelineLayout m_pipelineLayout = nullptr;
  std::unique_ptr<ShaderModule> m_shaderModule;
};


#endif //PIPELINE_H
