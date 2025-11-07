#ifndef TRANSFORM_H
#define TRANSFORM_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/detail/type_quat.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};

  [[nodiscard]] glm::mat4 toMat4() const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * R * S;
  }
};

#endif //TRANSFORM_H
