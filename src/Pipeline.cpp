#include "Pipeline.h"

vk::Pipeline PipelineBuilder::build() {
  std::vector shaderStages = {
    m_shaderModule->vertexPipelineInfo, m_shaderModule->fragmentPipelineInfo
  };

  std::vector dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
  };

  auto bindingDescription = m_bindingDescriptions;
  auto attributeDescription = m_attributeDescriptions;
  auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo({}, dynamicStates);
  auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo({}, bindingDescription, attributeDescription);
  auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList, false);
  auto viewportState = vk::PipelineViewportStateCreateInfo({}, 1, nullptr, 1, nullptr);
  auto rasterizer = vk::PipelineRasterizationStateCreateInfo(
    {}, false, false, vk::PolygonMode::eFill,
    vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise,
    false, {}, {}, {}, 1.0f);
  auto multisampling = vk::PipelineMultisampleStateCreateInfo({}, m_msaaSamples, m_msaaEnabled, m_msaaMinSample);
  auto depthStencil = vk::PipelineDepthStencilStateCreateInfo(
    {}, m_depthTestEnabled, m_depthWriteEnabled, m_depthCompareOp
  );
  depthStencil.setDepthBoundsTestEnable(false)
      .setMinDepthBounds(0.0f)
      .setMaxDepthBounds(1.0f);
  std::vector colorAttachments =
      m_colorBlendAttachments.has_value()
        ? m_colorBlendAttachments.value()
        : makeDefaultColorAttachmentStates();
  auto colorBlend = vk::PipelineColorBlendStateCreateInfo({}, false, vk::LogicOp::eCopy, colorAttachments);

  auto pipelineInfo = vk::GraphicsPipelineCreateInfo({});
  pipelineInfo.setStages(shaderStages)
      .setPVertexInputState(&vertexInputInfo)
      .setPInputAssemblyState(&inputAssembly)
      .setPViewportState(&viewportState)
      .setPRasterizationState(&rasterizer)
      .setPMultisampleState(&multisampling)
      .setPDepthStencilState(&depthStencil)
      .setPColorBlendState(&colorBlend)
      .setPDynamicState(&dynamicStateInfo)
      .setLayout(m_pipelineLayout)
      .setRenderPass(m_renderPass)
      .setSubpass(m_subpass)
      .setBasePipelineHandle(VK_NULL_HANDLE)
      .setBasePipelineIndex(-1);

  auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
  if (result.result != vk::Result::eSuccess) {
    std::cerr << "Failed to create pipeline!" << std::endl;
    abort();
  }
  return result.value;
}
