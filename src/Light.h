#ifndef LIGHT_H
#define LIGHT_H

#include "utils.cpp"

#define MAX_LIGHTS 64

enum class LightType {
  SPOT = -1,
  DIRECTIONAL = 0,
  POINT = 1
};

struct alignas(16) LightData {
  glm::vec4 position; // .xyz = position, .w = light type
  glm::vec4 color; // .rgb = color, .w = intensity
  glm::vec4 direction; // .xyz = light direction or vector, .w = constant attenuation (for point/spot)
  glm::vec4 info; // .x/.y = inner/outer cone angle (for spotlights), .z = linear attenuation, .w = exp attenuation
};

struct alignas(16) LightPushConsts {
  uint32_t lightCount;
};

class LightManager {
public:
  LightManager() = default;

  ~LightManager() = default;

  LightManager(const vma::Allocator allocator, const uint32_t imageCount) : m_allocator(allocator) {
    constexpr auto bufferSize = sizeof(LightData) * MAX_LIGHTS;
    m_ssboBuffers.resize(imageCount);
    m_ssboAllocations.resize(imageCount);
    m_bufferInfos.resize(imageCount);

    for (int i = 0; i < imageCount; ++i) {
      std::tie(m_ssboBuffers[i], m_ssboAllocations[i]) = createBufferUnique(
        allocator, bufferSize, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

      if (!m_ssboBuffers[i] || !m_ssboAllocations[i]) {
        throw std::runtime_error("Failed to create SSBO for light!");
      }

      m_bufferInfos[i] = vk::DescriptorBufferInfo(m_ssboBuffers[i].get(), 0, bufferSize);
    }
  }

  [[nodiscard]] std::vector<vk::DescriptorBufferInfo> getBufferInfos() const {
    return m_bufferInfos;
  }

  [[nodiscard]] uint32_t getCount() const { return m_lights.size(); }

  void map(uint32_t imageIndex) {
    auto mapped = m_allocator.mapMemory(m_ssboAllocations[imageIndex].get());
    auto *dst = static_cast<LightData *>(mapped);
    std::ranges::copy(m_lights, dst);
    m_allocator.unmapMemory(m_ssboAllocations[imageIndex].get());
  }

  void renderImGui() {
    if (ImGui::Begin("Lighting")) {
      if (ImGui::Button("Add light") && m_lights.size() < MAX_LIGHTS) {
        LightData light{};
        light.position = glm::vec4(0.0f, 0.0f, 0.0f, LightType::DIRECTIONAL);
        light.color = glm::vec4(1.0f);
        light.direction = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
        light.info = glm::vec4(0.0f, 0.0f, 0.35f, 0.44f);
        m_lights.push_back(light);
      }
      ImGui::Separator();

      for (size_t i = 0; i < m_lights.size(); ++i) {
        LightData &light = m_lights[i];
        ImGui::PushID(static_cast<int>(i));

        int type = static_cast<int>(light.position.w);
        ImGui::Text("Type: ");
        ImGui::SameLine();
        if (ImGui::RadioButton("Spot", type == static_cast<int>(LightType::SPOT))) {
          type = static_cast<int>(LightType::SPOT);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Directional", type == static_cast<int>(LightType::DIRECTIONAL))) {
          type = static_cast<int>(LightType::DIRECTIONAL);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Point", type == static_cast<int>(LightType::POINT))) {
          type = static_cast<int>(LightType::POINT);
        }
        light.position.w = static_cast<float>(type);

        if (type != static_cast<int>(LightType::DIRECTIONAL))
          ImGui::DragFloat3("Position", &light.position.x, 0.1f);

        if (type != static_cast<int>(LightType::POINT))
          ImGui::DragFloat3("Direction", &light.direction.x, 0.1f);

        ImGui::ColorEdit3("Color", &light.color.x);
        ImGui::DragFloat("Intensity", &light.color.w, 0.01f, 0.0f, 10.0f);

        if (type == static_cast<int>(LightType::SPOT)) {
          ImGui::DragFloat("Inner Cone", &light.info.x, 0.01f, 0.0f, 1.0f);
          ImGui::DragFloat("Outer Cone", &light.info.y, 0.01f, 0.0f, 1.0f);
        }

        if (type != static_cast<int>(LightType::DIRECTIONAL)) {
          ImGui::DragFloat("Linear Attenuation", &light.info.z, 0.01f, 0.0f, 10.0f);
          ImGui::DragFloat("Quadratic Attenuation", &light.info.w, 0.01f, 0.0f, 10.0f);
        }

        if (ImGui::Button("Remove")) {
          m_lights.erase(m_lights.begin() + i);
          --i;
        }

        ImGui::Separator();
        ImGui::PopID();
      }
    }
    ImGui::End();
  }

private:
  vma::Allocator m_allocator;
  std::vector<LightData> m_lights = {};
  bool m_uiOpen = true;

  std::vector<vma::UniqueBuffer> m_ssboBuffers = {};
  std::vector<vma::UniqueAllocation> m_ssboAllocations = {};
  std::vector<vk::DescriptorBufferInfo> m_bufferInfos = {};
};

#endif //LIGHT_H
