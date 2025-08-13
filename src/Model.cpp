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
  const std::filesystem::path &modelPath
) {
  std::cout << "Loading model from: " << modelPath.string() << std::endl;
  Assimp::Importer importer;

  const aiScene *scene = importer.ReadFile(
    modelPath.string(),
    aiProcess_Triangulate
    | aiProcess_JoinIdenticalVertices
    //| aiProcess_PreTransformVertices
    //| aiProcess_GenSmoothNormals
  );
  if (!scene)
    throw std::runtime_error("Import of model failed");

  createMesh(device, graphicsQueue, commandPool, allocator, scene);
  const auto modelParent = modelPath.parent_path();
  loadTextures(device, graphicsQueue, commandPool, allocator, scene, modelParent);
  m_sampler = createSamplerUnique(device);
  createTextureInfos();
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
    auto texturePath = getMaterialAlbedoTextureFile(meshMaterial);

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
        auto texCord = texCords ? texCords[vertexIndex] : aiVector3D(0, 0, 0);

        glm::vec4 transformedPos = transform * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
        glm::vec3 transformedNormal = glm::mat3(glm::transpose(glm::inverse(transform))) *
                                      glm::vec3(normal.x, normal.y, normal.z);

        auto vert = Vertex{
          .Position = transformedPos,
          .Normal = glm::normalize(transformedNormal),
          .TexCoords = glm::vec2(texCord.x, 1.0f - texCord.y),
          .Color = diffuseColor,
          .TextureIdx = texturePath.has_value() ? mesh->mMaterialIndex : 0
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

void Model::loadTextures(
  const vk::Device device,
  const vk::Queue graphicsQueue,
  const vk::CommandPool commandPool,
  const vma::Allocator allocator,
  const aiScene *scene,
  const std::filesystem::path &modelParent
) {
  std::vector<std::pair<uint32_t, std::string> > texturesFiles;
  texturesFiles.reserve(scene->mNumTextures);

  for (unsigned int matIdx = 0; matIdx < scene->mNumMaterials; ++matIdx) {
    aiMaterial *material = scene->mMaterials[matIdx];
    if (const auto path = getMaterialAlbedoTextureFile(material); path.has_value())
      texturesFiles.emplace_back(matIdx, path.value());
  }

  const auto absoluteModelParent = std::filesystem::canonical(modelParent);
  for (const auto &[matIdx, texFile]: texturesFiles) {
    std::filesystem::path texturePath = absoluteModelParent / texFile;
    std::cout << "Loading texture: " << texturePath << "\n";

    auto texture = Texture::createFromFile(
      device,
      allocator,
      graphicsQueue,
      commandPool,
      texturePath
    );

    /*vk::Image::generateMipmaps(
        device,
        graphicsQueue,
        commandPool,
        texture.image,
        vk::Format::eR8G8B8A8Srgb,
        texture.width,
        texture.height,
        texture.mipLevels
    );*/

    this->m_textures[matIdx] = std::move(texture);
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

void Model::updateTextureDescriptors(
  const vk::Device device,
  const DescriptorSet &descriptorSet,
  const uint32_t imageCount,
  const uint32_t shaderBinding
) {
  for (int frameIdx = 0; frameIdx < imageCount; ++frameIdx) {
    for (auto &[matIdx, texDescriptor]: m_textureDescriptors) {
      descriptorSet.updateTexture(device, frameIdx, shaderBinding, matIdx, texDescriptor);
    }
  }
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
