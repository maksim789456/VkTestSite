#include "Model.h"

static std::optional<std::string> getMaterialAlbedoTextureFile(
  aiMaterial *material
) {
  if (!material)
    return std::nullopt;

  aiString path;

  if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS) {
    return std::string(path.C_Str());
  }

  if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS) {
    return std::string(path.C_Str());
  }

  return std::nullopt;
}

static std::optional<std::string> getMaterialNormalTextureFile(
  aiMaterial *material
) {
  if (!material)
    return std::nullopt;

  aiString path;

  if (material->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS) {
    return std::string(path.C_Str());
  }

  if (material->GetTexture(aiTextureType_HEIGHT, 0, &path) == AI_SUCCESS) {
    return std::string(path.C_Str());
  }

  return std::nullopt;
}

Model::Model(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  vma::Allocator allocator,
  TextureManager &textureManager,
  LightManager &lightManager,
  const std::filesystem::path &modelPath
): m_device(device), m_graphicsQueue(graphicsQueue), m_commandPool(commandPool), m_allocator(allocator) {
  ZoneScoped;
  spdlog::info(std::format("Loading model from: {}", modelPath.string()));
  Assimp::Importer importer;

  const aiScene *scene = importer.ReadFile(
    modelPath.string(),
    aiProcess_Triangulate
    | aiProcess_JoinIdenticalVertices
    | aiProcess_GenNormals
    | aiProcess_CalcTangentSpace
  );
  if (!scene)
    throw std::runtime_error("Import of model failed");

  const auto modelParent = modelPath.parent_path();
  processMaterials(textureManager, scene, modelParent);
  processLight(lightManager, scene);
  processNode(lightManager, scene->mRootNode, scene, glm::mat4(1.0f));
  m_name = std::string(scene->mRootNode->mName.C_Str());
}

void Model::processNode(
  LightManager &lightManager,
  const aiNode *node,
  const aiScene *scene,
  const glm::mat4 &parentTransform
) {
  ZoneScoped;
  auto nodeTransform = aiMatrix4x4ToGlm(node->mTransformation);
  auto globalTransform = parentTransform * nodeTransform;

  std::string nodeName(node->mName.C_Str());
  const auto lightNames = lightManager.getNames();
  const auto lights = lightManager.getLights();
  for (uint32_t i = 0; i < lightNames.size(); ++i) {
    if (lightNames[i] == nodeName) {
      LightData light = lights[i];

      const auto position = glm::vec3(globalTransform[3]);
      light.position.x = position.x;
      light.position.y = position.y;
      light.position.z = position.z;

      lightManager.editLight(i, light);
      break;
    }
  }

  for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[m]];
    auto gpuMesh = createMesh(mesh, scene, globalTransform);
    m_submeshes.push_back({
      .mesh = std::move(gpuMesh),
      .materialIndex = mesh->mMaterialIndex,
      .transform = globalTransform,
      .name = mesh->mName.C_Str()
    });
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i)
    processNode(lightManager, node->mChildren[i], scene, globalTransform);
}

void Model::processMaterials(
  TextureManager &textureManager,
  const aiScene *scene,
  const std::filesystem::path &modelParent
) {
  ZoneScoped;
  const auto absoluteModelParent = std::filesystem::canonical(modelParent);
  m_materials.resize(scene->mNumMaterials);

  for (unsigned int matIdx = 0; matIdx < scene->mNumMaterials; ++matIdx) {
    aiMaterial *material = scene->mMaterials[matIdx];
    Material mat;
    if (const auto albedo = getMaterialAlbedoTextureFile(material))
      mat.albedoTexIdx = textureManager.loadTextureFromFile(absoluteModelParent, *albedo);

    if (const auto normal = getMaterialNormalTextureFile(material))
      mat.normalTexIdx = textureManager.loadTextureFromFile(absoluteModelParent, *normal);

    if (aiColor3D aiDiffuseColor; material->Get(AI_MATKEY_COLOR_DIFFUSE, aiDiffuseColor) == AI_SUCCESS) {
      mat.diffuseColor = glm::vec4(aiDiffuseColor.r, aiDiffuseColor.g, aiDiffuseColor.b, 1.0f);
    }

    m_materials[matIdx] = mat;
  }
}

void Model::processLight(LightManager &lightManager, const aiScene *scene) {
  for (int i = 0; i < scene->mNumLights; ++i) {
    const aiLight *sceneLight = scene->mLights[i];
    LightData light{};
    float type = 0.0f;
    switch (sceneLight->mType) {
      case aiLightSource_DIRECTIONAL:
        type = static_cast<float>(LightType::DIRECTIONAL);
        break;
      case aiLightSource_POINT:
        type = static_cast<float>(LightType::POINT);
        break;
      case aiLightSource_SPOT:
        type = static_cast<float>(LightType::SPOT);
        break;
      default:
        type = static_cast<float>(LightType::POINT);
        break;
    }
    light.position = glm::vec4(sceneLight->mPosition.x, sceneLight->mPosition.y, sceneLight->mPosition.z, type);
    float intensity =
        sceneLight->mColorDiffuse.r > sceneLight->mColorDiffuse.b
          ? sceneLight->mColorDiffuse.r > sceneLight->mColorDiffuse.g
              ? sceneLight->mColorDiffuse.r
              : sceneLight->mColorDiffuse.g
          : sceneLight->mColorDiffuse.b;

    light.color = glm::vec4(
      sceneLight->mColorDiffuse.r / intensity,
      sceneLight->mColorDiffuse.g / intensity,
      sceneLight->mColorDiffuse.b / intensity,
      intensity / (sceneLight->mType != aiLightSource_DIRECTIONAL ? 1000.f : 100.f) //TODO: Temp fix for Candela/Lux units
    );

    light.direction = glm::vec4(sceneLight->mDirection.x, sceneLight->mDirection.y, sceneLight->mDirection.z,
                                sceneLight->mAttenuationConstant);

    // Cone angles converted from radians → cosine
    float innerCos = std::cos(sceneLight->mAngleInnerCone * 0.5f);
    float outerCos = std::cos(sceneLight->mAngleOuterCone * 0.5f);
    light.info = glm::vec4(
      innerCos,
      outerCos,
      sceneLight->mAttenuationLinear,
      sceneLight->mAttenuationQuadratic
    );
    lightManager.addLight(light, sceneLight->mName.C_Str());
  }
}

std::unique_ptr<Mesh<Vertex, uint32_t> > Model::createMesh(
  const aiMesh *mesh,
  const aiScene *scene,
  const glm::mat4 &transform
) {
  ZoneScoped;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  auto texCords = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0] : nullptr;
  for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
    aiFace &face = mesh->mFaces[f];
    for (unsigned int i = 0; i < face.mNumIndices; ++i) {
      auto vertexIndex = face.mIndices[i];
      auto pos = mesh->mVertices[vertexIndex];
      auto normal = mesh->HasNormals() ? mesh->mNormals[vertexIndex] : aiVector3D(0, 0, 1.0);
      auto texCord = texCords ? texCords[vertexIndex] : aiVector3D(0, 0, 0);
      auto mat = m_materials[mesh->mMaterialIndex];

      glm::vec4 transformedPos = transform * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
      auto normalMat = glm::mat3(glm::transpose(glm::inverse(transform)));
      auto N = glm::normalize(normalMat * glm::vec3(normal.x, normal.y, normal.z));

      vertices.push_back({
        .Position = transformedPos,
        .Normal = N,
        .UV = glm::vec2(texCord.x, 1.0f - texCord.y),
        .Color = mat.diffuseColor,
        .TextureIdx = mat.albedoTexIdx,
        .NormalTextureIdx = mat.normalTexIdx
      });

      //indices.push_back(baseIndex + static_cast<uint32_t>(vertices.size() - baseIndex - 1));
      indices.push_back(static_cast<uint32_t>(indices.size()));
    }
  }

  return std::make_unique<Mesh<Vertex, uint32_t> >(
    m_allocator, m_device, m_graphicsQueue, m_commandPool, vertices, indices
  );
}

void Model::createCommandBuffers(
  const vk::Device device,
  const vk::CommandPool commandPool,
  const uint32_t imagesCount
) {
  ZoneScoped;
  const auto info = vk::CommandBufferAllocateInfo(
    commandPool, vk::CommandBufferLevel::eSecondary, imagesCount
  );
  m_commandBuffers = device.allocateCommandBuffersUnique(info);
}

inline ModelPushConsts Model::calcPushConsts(const glm::mat4 &transform) const {
  return ModelPushConsts{
    .model = m_transform.toMat4() * transform
  };
}

vk::CommandBuffer Model::cmdDraw(
  tracy::VkCtx &tracyCtx,
  const vk::Framebuffer framebuffer,
  const vk::RenderPass renderPass,
  const vk::Pipeline pipeline,
  const Swapchain &swapchain,
  const DescriptorSet &descriptorSet,
  const uint32_t subpass,
  const uint32_t imageIndex
) {
  ZoneScoped;
  const auto inheritanceInfo = vk::CommandBufferInheritanceInfo(renderPass, subpass, framebuffer);
  const auto beginInfo = vk::CommandBufferBeginInfo(
    vk::CommandBufferUsageFlagBits::eRenderPassContinue | vk::CommandBufferUsageFlagBits::eSimultaneousUse,
    &inheritanceInfo
  );

  const auto cmdBuf = m_commandBuffers[imageIndex].get();
  const auto push_consts = calcPushConsts();

  cmdBuf.reset();
  cmdBuf.begin(beginInfo);

  swapchain.cmdSetViewport(cmdBuf);
  swapchain.cmdSetScissor(cmdBuf);
  cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

  for (const auto &sub: m_submeshes) {
    if (!sub.enabled) {
      continue;
    }
    const auto push_consts = calcPushConsts(sub.transform);
    cmdBuf.bindVertexBuffers(0, sub.mesh->getVertexBuffer(), {0});
    cmdBuf.bindIndexBuffer(sub.mesh->getIndicesBuffer(), 0, vk::IndexType::eUint32);
    descriptorSet.bind(cmdBuf, imageIndex, {});
    cmdBuf.pushConstants(descriptorSet.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex,
                         0, sizeof(push_consts), &push_consts);

    cmdBuf.drawIndexed(sub.mesh->getIndicesCount(), 1, 0, 0, 0);
  }
  cmdBuf.end();

  return cmdBuf;
}

void Model::drawUI() {
  if (ImGui::Begin("Model Inspector")) {
    ImGui::Text("Model: %s", m_name.c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::DragFloat3("Position", &m_transform.position.x, 0.05f);

      glm::vec3 euler = glm::degrees(glm::eulerAngles(m_transform.rotation));
      if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
        m_transform.rotation = glm::quat(glm::radians(euler));
      }

      ImGui::DragFloat3("Scale", &m_transform.scale.x, 0.05f, 0.001f, 100.0f);
    }

    if (ImGui::CollapsingHeader("Submeshes", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (size_t i = 0; i < m_submeshes.size(); ++i) {
        auto &sub = m_submeshes[i];
        std::string label = sub.name.empty() ? "Submesh " + std::to_string(i) : sub.name;

        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_None)) {
          ImGui::Checkbox("Enabled", &sub.enabled);
          auto subTransform = Transform{};
          subTransform.fromMat4(sub.transform);
          ImGui::DragFloat3("Position", &subTransform.position.x, 0.05f);

          glm::vec3 euler = glm::degrees(glm::eulerAngles(subTransform.rotation));
          if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
            subTransform.rotation = glm::quat(glm::radians(euler));
          }

          ImGui::DragFloat3("Scale", &subTransform.scale.x, 0.05f, 0.001f, 100.0f);
          //sub.transform = subTransform.toMat4();
          ImGui::TreePop();
        }
      }
    }
  }
  ImGui::End();
}
