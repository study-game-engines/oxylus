#pragma once

#include "glm/vec4.hpp"
#include "Core/Base.h"
#include "Render/Vulkan/VulkanDescriptorSet.h"
#include "Render/Vulkan/VulkanImage.h"
#include "Render/Vulkan/VulkanShader.h"

namespace Oxylus {
  class Material {
  public:
    enum class AlphaMode {
      Opaque = 0,
      Mask,
      Blend
    } AlphaMode = AlphaMode::Opaque;

    struct Parameters {
      Vec4 Color = Vec4(1.0f);
      Vec4 Emmisive = Vec4(0);
      float Roughness = 1.0f;
      float Metallic = 0.0f;
      float Specular = 0.0f;
      float Normal = 1.0f;
      float AO = 1.0f;
      GLSL_BOOL UseAlbedo = false;
      GLSL_BOOL UseRoughness = false;
      GLSL_BOOL UseMetallic = false;
      GLSL_BOOL UseNormal = false;
      GLSL_BOOL UseAO = false;
      GLSL_BOOL UseEmissive = false;
      GLSL_BOOL UseSpecular = false;
      GLSL_BOOL FlipImage = false;
      float AlphaCutoff = 1;
      GLSL_BOOL DoubleSided = false;
      uint32_t UVScale = 1;
    } Parameters;

    std::string Name = "Material";
    std::string Path{};

    static VulkanDescriptorSet s_DescriptorSet;
    VulkanDescriptorSet MaterialDescriptorSet;
    VulkanDescriptorSet DepthDescriptorSet;
    Ref<VulkanShader> Shader = nullptr;
    Ref<VulkanImage> AlbedoTexture = nullptr;
    Ref<VulkanImage> NormalTexture = nullptr;
    Ref<VulkanImage> RoughnessTexture = nullptr;
    Ref<VulkanImage> MetallicTexture = nullptr;
    Ref<VulkanImage> AOTexture = nullptr;
    Ref<VulkanImage> EmissiveTexture = nullptr;
    Ref<VulkanImage> SpecularTexture = nullptr;
    Ref<VulkanImage> DiffuseTexture = nullptr;

    Material() = default;
    ~Material();

    void Create(const std::string& name = "Material");
    bool IsOpaque() const;
    void Update();
    void Destroy();
  private:
    Ref<VulkanShader> m_Shader = nullptr;

    void ClearTextures();
  };
}
