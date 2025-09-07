#ifndef VERTEX_H
#define VERTEX_H

#include <vulkan/vulkan.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class Vertex {
public:
  glm::vec3 Position;
  glm::vec3 Normal;
  glm::vec2 UV;
  glm::vec4 Color;
  uint32_t TextureIdx;
  uint32_t NormalTextureIdx;

  static vk::VertexInputBindingDescription GetBindingDescription() {
    return vk::VertexInputBindingDescription(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
  }

  static std::vector<vk::VertexInputAttributeDescription> GetAttributeDescriptions() {
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.push_back(
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position)));
    attributeDescriptions.push_back(
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal)));
    attributeDescriptions.push_back(
      vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, UV)));
    attributeDescriptions.push_back(
      vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Color)));
    attributeDescriptions.push_back(
      vk::VertexInputAttributeDescription(5, 0, vk::Format::eR32Uint, offsetof(Vertex, TextureIdx)));
    attributeDescriptions.push_back(
      vk::VertexInputAttributeDescription(6, 0, vk::Format::eR32Uint, offsetof(Vertex, NormalTextureIdx)));
    return attributeDescriptions;
  }
};

#endif
