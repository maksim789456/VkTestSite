#ifndef VKTESTSITE_MATERIALMANAGER_H
#define VKTESTSITE_MATERIALMANAGER_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_float4x4.hpp>

#include "DescriptorSet.h"
#include "Ubo.h"
#include "utils.cpp"

#define MAX_MATERIALS 512

struct alignas(16) Material {
  glm::ivec4 textureIds = glm::ivec4(99, 99, 99, 99); //albedo, normal, resv, resv
  glm::vec4 diffuseColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

  bool isEmpty() const {
    return diffuseColor.w == 0.0f;
  }
};

class MaterialManager {
public:
  MaterialManager(
    vma::Allocator allocator,
    uint32_t imagesCount,
    uint32_t shaderBinding
  );

  ~MaterialManager() = default;

  void map(uint32_t imageIndex) const;

  uint32_t addMaterial(Material &material);

  void removeMaterial(uint32_t slot);

  std::optional<const Material *> getMaterial(uint32_t slot) const;

  DescriptorLayout getDescriptorLayout() const { return m_materialsUbo->getDescriptorLayout(); }

private:
  struct alignas(16) MaterialUbo {
    Material materials[MAX_MATERIALS];
  };

  std::array<Material, MAX_MATERIALS> m_materials = {};
  std::unique_ptr<UniformBuffer<MaterialUbo> > m_materialsUbo;
};


#endif //VKTESTSITE_MATERIALMANAGER_H
