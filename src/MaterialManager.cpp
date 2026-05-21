#include "MaterialManager.h"

MaterialManager::MaterialManager(
  const vma::Allocator allocator,
  const uint32_t imagesCount,
  const uint32_t shaderBinding
) {
  m_materialsUbo = std::make_unique<UniformBuffer<MaterialUbo> >(
    allocator,
    imagesCount,
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    shaderBinding
  );
}

void MaterialManager::map(const uint32_t imageIndex) const {
  auto ubo = MaterialUbo();
  memcpy(ubo.materials, m_materials.data(), m_materials.size());
  m_materialsUbo->map(imageIndex, ubo);
}

uint32_t MaterialManager::addMaterial(Material &material) {
  for (int i = 0; i < m_materials.size(); ++i) {
    if (m_materials[i].isEmpty()) {
      m_materials[i] = material;
      m_materials[i].diffuseColor.w = 1.0f;
      return i;
    }
  }

  spdlog::error("Material buffer is full, unable to push new material(albedo {}, norm {})",
                material.textureIds.x, material.textureIds.y);
  return -1;
}

void MaterialManager::removeMaterial(const uint32_t slot) {
  m_materials[slot] = Material{};
}

std::optional<const Material *> MaterialManager::getMaterial(const uint32_t slot) const {
  auto mat = &m_materials[slot];
  if (mat->isEmpty()) {
    return std::nullopt;
  }

  return mat;
}
