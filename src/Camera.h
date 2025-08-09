#ifndef CAMERA_H
#define CAMERA_H

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

class Camera {
public:
  void onUpdate(const float deltaTime) {
    const auto rotationMatrix = glm::mat4_cast(rotation);
    const glm::vec3 offset = rotationMatrix * glm::vec4(velocity * deltaTime * 5.0f, 0.0f);
    position += offset;

    const auto translation = glm::translate(glm::mat4(1.0f), position);
    const auto view = glm::inverse(translation * rotationMatrix);
    auto proj = glm::perspective(fov, aspectRatio, zNear, zFar);
    proj[1][1] *= -1;
    viewProj = proj * view;
  }

  void keyboardCallback(int key, int action) {
    const float value = action == GLFW_PRESS || action == GLFW_REPEAT ? 1.0f : 0.0f;

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

    rotation = glm::normalize(rotation * (pitch_rotation * yaw_rotation));
  }

  [[nodiscard]] glm::mat4 getViewProj() const { return viewProj; }
  [[nodiscard]] glm::vec3 getViewPos() const { return position; }

  float aspectRatio = 1.33f;

private:
  float fov = glm::radians(45.0f);
  float zNear = 0.1f;
  float zFar = 100.0f;

  glm::vec3 position = glm::vec3(0.0f, 1.2f, 0.6f);
  glm::quat rotation = glm::quat(glm::vec3(
    glm::radians(-90.0f),
    glm::radians(50.0f),
    glm::radians(180.0f)));
  glm::vec3 velocity = glm::vec3(0.0f);

  bool first_move = false;
  double last_x = 0, last_y = 0;

  glm::mat4 viewProj = glm::mat4(1.0f);
};

#endif //CAMERA_H
