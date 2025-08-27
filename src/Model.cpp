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
  TextureManager& textureManager,
  const std::filesystem::path &modelPath
) {
  std::cout << "Loading model from: " << modelPath.string() << std::endl;
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
  createMesh(device, graphicsQueue, commandPool, allocator, scene);
  m_name = std::string(scene->mRootNode->mName.C_Str());
}

void Model::createMesh(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  vma::Allocator allocator,
  const aiScene *scene
) {
  std::vector<Vertex> vertices = {};
  std::vector<uint32_t> indices = {};

  auto modelMat = glm::translate(glm::mat4(1), m_position);
  processNode(scene->mRootNode, scene, modelMat, vertices, indices);

  std::cout << "Convert success: Vertices: " << vertices.size() << "; Indices: " << indices.size() << std::endl;

  m_mesh = std::make_unique<Mesh<Vertex, uint32_t> >(
    allocator, device, graphicsQueue, commandPool, vertices, indices
  );
}

void Model::processNode(
  const aiNode *node,
  const aiScene *scene,
  const glm::mat4 &parentTransform,
  std::vector<Vertex> &vertices,
  std::vector<uint32_t> &indices) {
  auto transform = parentTransform * aiMatrix4x4ToGlm(node->mTransformation);

  for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[m]];
    std::cout << "Node: " << node->mName.C_Str() << "; Vertices count: " << mesh->mNumVertices << std::endl;
    auto texCords = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0] : nullptr;
    auto meshMaterial = scene->mMaterials[mesh->mMaterialIndex];
    auto albedoIdx = m_albedoMapping.find(mesh->mMaterialIndex);
    auto normalIdx = m_normalMapping.find(mesh->mMaterialIndex);

    glm::vec4 diffuseColor(1.0f);
    if (aiColor3D aiDiffuseColor; meshMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiDiffuseColor) == AI_SUCCESS) {
      diffuseColor = glm::vec4(aiDiffuseColor.r, aiDiffuseColor.g, aiDiffuseColor.b, 1.0f);
    }

    uint32_t baseIndex = vertices.size();

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      aiFace &face = mesh->mFaces[f];
      for (unsigned int i = 0; i < face.mNumIndices; ++i) {
        auto vertexIndex = face.mIndices[i];
        auto pos = mesh->mVertices[vertexIndex];
        auto normal = mesh->HasNormals() ? mesh->mNormals[vertexIndex] : aiVector3D(0, 0, 0);
        auto tangent = mesh->HasTangentsAndBitangents() ? mesh->mTangents[vertexIndex] : aiVector3D(0, 0, 0);
        auto texCord = texCords ? texCords[vertexIndex] : aiVector3D(0, 0, 0);

        glm::vec4 transformedPos = transform * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
        auto normalMat = glm::mat3(glm::transpose(glm::inverse(transform)));
        auto N = glm::normalize(normalMat * glm::vec3(normal.x, normal.y, normal.z));
        auto T = glm::normalize(normalMat * glm::vec3(tangent.x, tangent.y, tangent.z));
        T = glm::normalize(T - N * glm::dot(N, T));

        auto vert = Vertex{
          .Position = transformedPos,
          .Normal = N, .Tangent = T,
          .UV = glm::vec2(texCord.x, 1.0f - texCord.y),
          .Color = diffuseColor,
          .TextureIdx = albedoIdx != m_albedoMapping.end() ? albedoIdx->second : 99,
          .NormalTextureIdx = normalIdx != m_normalMapping.end() ? normalIdx->second : 99,
        };

        vertices.push_back(vert);
        indices.push_back(baseIndex + static_cast<uint32_t>(vertices.size() - baseIndex - 1));
      }
    }
  }

  for (int i = 0; i < node->mNumChildren; ++i) {
    processNode(node->mChildren[i], scene, transform, vertices, indices);
  }
}

void Model::processMaterials(
  TextureManager& textureManager,
  const aiScene *scene,
  const std::filesystem::path &modelParent
) {
  const auto absoluteModelParent = std::filesystem::canonical(modelParent);

  for (unsigned int matIdx = 0; matIdx < scene->mNumMaterials; ++matIdx) {
    aiMaterial *material = scene->mMaterials[matIdx];
    if (const auto path = getMaterialAlbedoTextureFile(material); path.has_value())
      m_albedoMapping[matIdx] = textureManager.loadTextureFromFile(absoluteModelParent, path.value());

    if (const auto path = getMaterialNormalTextureFile(material); path.has_value())
      m_normalMapping[matIdx] = textureManager.loadTextureFromFile(absoluteModelParent, path.value());
  }
}

void Model::createCommandBuffers(
  const vk::Device device,
  const vk::CommandPool commandPool,
  const uint32_t imagesCount
) {
  const auto info = vk::CommandBufferAllocateInfo(
    commandPool, vk::CommandBufferLevel::eSecondary, imagesCount
  );
  m_commandBuffers = device.allocateCommandBuffersUnique(info);
}

inline ModelPushConsts Model::calcPushConsts() const {
  return ModelPushConsts{
    .model = glm::translate(glm::mat4(1), m_position)
  };
}

vk::CommandBuffer Model::cmdDraw(
  const vk::Framebuffer framebuffer,
  const vk::RenderPass renderPass,
  const vk::Pipeline pipeline,
  const Swapchain &swapchain,
  const DescriptorSet &descriptorSet,
  const uint32_t subpass,
  const uint32_t imageIndex
) {
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
  cmdBuf.bindVertexBuffers(0, m_mesh->getVertexBuffer(), {0});
  cmdBuf.bindIndexBuffer(m_mesh->getIndicesBuffer(), 0, vk::IndexType::eUint32);
  descriptorSet.bind(cmdBuf, imageIndex, {});
  cmdBuf.pushConstants(descriptorSet.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex,
                       0, sizeof(push_consts), &push_consts);

  cmdBuf.drawIndexed(m_mesh->getIndicesCount(), 1, 0, 0, 0);
  cmdBuf.end();

  return cmdBuf;
}
