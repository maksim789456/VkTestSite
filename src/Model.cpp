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
  vk::Device device,
  vma::Allocator allocator,
  std::filesystem::path modelPath
) {
  Assimp::Importer importer;

  const aiScene *scene = importer.ReadFile(
    modelPath.string(),
    aiProcess_Triangulate
    | aiProcess_JoinIdenticalVertices
    | aiProcess_GenSmoothNormals
    | aiProcess_PreTransformVertices
  );

  const auto mesh = createMesh(allocator, scene);
}

Mesh<Vertex, uint32_t> *Model::createMesh(
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

    glm::vec3 diffuseColor(1.0f);
    if (aiColor3D aiDiffuseColor; meshMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiDiffuseColor) == AI_SUCCESS) {
      diffuseColor = glm::vec3(aiDiffuseColor.r, aiDiffuseColor.g, aiDiffuseColor.b);
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

  return new Mesh(allocator, vertices, indices);
}

void Model::createCommandBuffers(
  const vk::Device device,
  const vk::CommandPool commandPool,
  const uint32_t imagesCount
) {
  const auto info = vk::CommandBufferAllocateInfo(
    commandPool, vk::CommandBufferLevel::eSecondary, imagesCount
  );
  m_commandBuffers = device.allocateCommandBuffers(info);
}

inline ModelPushConsts Model::calcPushConsts() const {
  return ModelPushConsts{
    .model = glm::translate(glm::mat4(1), m_position)
  };
}
