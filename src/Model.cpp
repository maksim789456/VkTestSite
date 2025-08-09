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

Model::Model(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  vma::Allocator allocator,
  std::filesystem::path modelPath
) {
  std::cout << "Loading model from: " << modelPath.string() << std::endl;
  Assimp::Importer importer;

  const aiScene *scene = importer.ReadFile(
    modelPath.string(),
    aiProcess_Triangulate
    | aiProcess_JoinIdenticalVertices
    | aiProcess_PreTransformVertices
    //| aiProcess_GenSmoothNormals
  );
  if (!scene)
    throw std::runtime_error("Import of model failed");

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
  uint32_t index = 0;

  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    aiMesh *mesh = scene->mMeshes[m];
    std::cout << "Vertices count: " << mesh->mNumVertices << std::endl;
    auto texCords = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0] : nullptr;
    auto meshMaterial = scene->mMaterials[mesh->mMaterialIndex];
    auto texturePath = getMaterialAlbedoTextureFile(meshMaterial);

    glm::vec4 diffuseColor(1.0f);
    if (aiColor3D aiDiffuseColor; meshMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiDiffuseColor) == AI_SUCCESS) {
      diffuseColor = glm::vec4(aiDiffuseColor.r, aiDiffuseColor.g, aiDiffuseColor.b, 1.0f);
    }

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      aiFace &face = mesh->mFaces[f];
      for (unsigned int i = 0; i < face.mNumIndices; ++i) {
        auto vertexIndex = face.mIndices[i];
        auto pos = mesh->mVertices[vertexIndex];
        auto normal = mesh->HasNormals() ? mesh->mNormals[vertexIndex] : aiVector3D(0, 0, 0);
        auto texCord = texCords ? texCords[vertexIndex] : aiVector3D(0, 0, 0);

        auto vert = Vertex{
          .Position = glm::vec3(pos.x, pos.y, pos.z),
          .Normal = glm::vec3(normal.x, normal.y, normal.z),
          .TexCoords = glm::vec2(texCord.x, 1.0f - texCord.y),
          .Color = diffuseColor,
          .TextureIdx = texturePath.has_value() ? mesh->mMaterialIndex : 0
        };

        vertices.push_back(vert);
        indices.push_back(index);
        index += 1;
      }
    }
  }

  std::cout << "Convert success: Vertices: " << vertices.size() << "; Indices: " << indices.size() << std::endl;

  m_mesh = std::make_unique<Mesh<Vertex, uint32_t> >(
    allocator, device, graphicsQueue, commandPool, vertices, indices
  );
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
  //const auto push_consts = calcPushConsts();

  cmdBuf.reset();
  cmdBuf.begin(beginInfo);

  swapchain.cmdSetViewport(cmdBuf);
  swapchain.cmdSetScissor(cmdBuf);

  cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
  cmdBuf.bindVertexBuffers(0, m_mesh.get()->getVertexBuffer(), {0});
  cmdBuf.bindIndexBuffer(m_mesh.get()->getIndicesBuffer(), 0, vk::IndexType::eUint32);
  descriptorSet.bind(cmdBuf, imageIndex, {});
  //cmdBuf.pushConstants(descriptorSet.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex,
  //                     0, sizeof(push_consts), &push_consts);

  cmdBuf.drawIndexed(m_mesh.get()->getIndicesCount(), 1, 0, 0, 0);
  cmdBuf.end();

  return cmdBuf;
}
