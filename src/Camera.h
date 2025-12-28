#ifndef CAMERA_H
#define CAMERA_H

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

class Camera {
public:
  explicit Camera(const vk::Extent2D viewportSize) {
    aspectRatio = static_cast<float>(viewportSize.width) / static_cast<float>(viewportSize.height);
  }

  static glm::mat4 perspectiveRZ(float fovy, float aspect, float zNear, float zFar) {
    float f = 1.0f / tan(fovy * 0.5f);

    glm::mat4 result(0.0f);
    result[0][0] = f / aspect;
    result[1][1] = -f; // flip Y for Vulkan
    result[2][2] = zNear / (zFar - zNear);
    result[2][3] = -1.0f;
    result[3][2] = (zFar * zNear) / (zFar - zNear);
    return result;
  }

  void onUpdate(const float deltaTime) {
    const auto rotationMatrix = glm::mat4_cast(rotation);
    const glm::vec3 offset = rotationMatrix * glm::vec4(velocity * deltaTime * 5.0f, 0.0f);
    position += offset;

    const auto translation = glm::translate(glm::mat4(1.0f), position);
    view = glm::inverse(translation * rotationMatrix);
    proj = perspectiveRZ(fov, aspectRatio, zNear, zFar);
    viewProj = proj * view;

    updateFrustum();
  }

  void keyboardCallback(int key, int action, int mods) {
    float value = action == GLFW_PRESS || action == GLFW_REPEAT ? 1.0f : 0.0f;
    value *= mods == GLFW_MOD_SHIFT ? 2.0f : 1.0f;

    switch (key) {
      case GLFW_KEY_W: velocity.z = -value;
        break;
      case GLFW_KEY_S: velocity.z = value;
        break;
      case GLFW_KEY_A: velocity.x = -value;
        break;
      case GLFW_KEY_D: velocity.x = value;
        break;
      default: ;
    }
  }

  void mouseCallback(GLFWwindow *window, double xpos, double ypos) {
    bool left_pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (!left_pressed) {
      first_move = true;
      return;
    }

    if (first_move) {
      last_x = xpos;
      last_y = ypos;
      first_move = false;
      return;
    }

    const float sensitivity = 0.1f;
    float x_offset = static_cast<float>(xpos - last_x) * sensitivity;
    float y_offset = static_cast<float>(ypos - last_y) * sensitivity;

    last_x = xpos;
    last_y = ypos;

    glm::quat pitch_rotation = glm::angleAxis(
      glm::radians(y_offset),
      glm::vec3(1.0f, 0.0f, 0.0f)
    );
    glm::quat yaw_rotation = glm::angleAxis(
      glm::radians(x_offset),
      glm::vec3(0.0f, 1.0f, 0.0f)
    );

    rotation = glm::normalize(yaw_rotation * rotation * pitch_rotation);
  }

  void updateFrustum() {
    auto halfAngleY = glm::tan(fov * 0.5f);
    auto halfAngleX = halfAngleY * aspectRatio;

    frustumCorners = glm::vec4(halfAngleX, -halfAngleY, halfAngleX * zFar, -halfAngleY * zFar);
    invFrustumCorners = glm::vec4(1.f / halfAngleX, -1.f / halfAngleY, 1.f / (halfAngleX * zFar), -1.f / (halfAngleY * zFar));
  }

  [[nodiscard]] glm::mat4 getView() const { return view; }
  [[nodiscard]] glm::mat4 getProj() const { return proj; }
  [[nodiscard]] glm::mat4 getViewProj() const { return viewProj; }
  [[nodiscard]] glm::mat4 getInvViewProj() const { return glm::inverse(viewProj); }
  [[nodiscard]] glm::vec3 getViewPos() const { return position; }
  [[nodiscard]] glm::vec4 getFrustumCorners() const { return frustumCorners; }
  [[nodiscard]] glm::vec4 getInvFrustumCorners() const { return invFrustumCorners; }

  [[nodiscard]] float getZNear() const { return zNear; }
  [[nodiscard]] float getZFar() const { return zFar; }

  float aspectRatio = 1.77f;

private:
  float fov = glm::radians(45.0f);
  float zNear = 0.1f;
  float zFar = 1000.0f;

  glm::vec3 position = glm::vec3(0.0f, 1.2f, 0.6f);
  glm::quat rotation = glm::quat(glm::vec3(0.0f));
  glm::vec3 velocity = glm::vec3(0.0f);

  bool first_move = false;
  double last_x = 0, last_y = 0;

  glm::mat4 view = glm::mat4(1.0f);
  glm::mat4 proj = glm::mat4(1.0f);
  glm::mat4 viewProj = glm::mat4(1.0f);

  glm::vec4 frustumCorners;
  glm::vec4 invFrustumCorners;
};

#endif //CAMERA_H
