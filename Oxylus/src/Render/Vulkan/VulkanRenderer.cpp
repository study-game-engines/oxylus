#include "VulkanRenderer.h"

#include <future>

#include "VulkanCommandBuffer.h"
#include "VulkanContext.h"
#include "VulkanPipeline.h"
#include "VulkanSwapchain.h"
#include "Utils/VulkanUtils.h"
#include "Core/Resources.h"
#include "Render/Mesh.h"
#include "Render/Window.h"
#include "Render/Vulkan/VulkanBuffer.h"
#include "Render/ResourcePool.h"
#include "Render/ShaderLibrary.h"
#include "Utils/Profiler.h"

#include <backends/imgui_impl_vulkan.h>

#include "Render/DebugRenderer.h"

namespace Oxylus {
  VulkanSwapchain VulkanRenderer::s_SwapChain;
  VulkanRenderer::RendererContext VulkanRenderer::s_RendererContext;
  VulkanRenderer::RendererData VulkanRenderer::s_RendererData;
  VulkanRenderer::Pipelines VulkanRenderer::s_Pipelines;
  Ref<DescriptorPoolManager> VulkanRenderer::s_DescriptorPoolManager = nullptr;
  Ref<CommandPoolManager> VulkanRenderer::s_CommandPoolManager = nullptr;
  RendererConfig VulkanRenderer::s_RendererConfig;

  Ref<DefaultRenderPipeline> VulkanRenderer::s_DefaultPipeline = nullptr;

  static VulkanBuffer s_TriangleVertexBuffer;
  static VulkanDescriptorSet s_QuadDescriptorSet;

  void VulkanRenderer::ResizeBuffers() {
    WaitDeviceIdle();
    WaitGraphicsQueueIdle();
    s_SwapChain.RecreateSwapChain();
    FrameBufferPool::ResizeBuffers();
    ImagePool::ResizeImages();
    s_SwapChain.Resizing = false;
  }

  void VulkanRenderer::Init() {
    //TODO: Move
    {
      // Save/Load renderer config
      if (!RendererConfig::Get()->LoadConfig("renderer.oxconfig")) {
        RendererConfig::Get()->SaveConfig("renderer.oxconfig");
      }
      RendererConfig::Get()->ConfigChangeDispatcher.trigger(RendererConfig::ConfigChangeEvent{});
    }

    const auto& LogicalDevice = VulkanContext::GetDevice();

    // Pool managers
    s_DescriptorPoolManager = CreateRef<DescriptorPoolManager>();
    s_DescriptorPoolManager->Init();

    s_CommandPoolManager = CreateRef<CommandPoolManager>();
    s_CommandPoolManager->Init();

    s_SwapChain.SetVsync(RendererConfig::Get()->DisplayConfig.VSync, false);
    s_SwapChain.CreateSwapChain();

    s_RendererContext.m_TimelineCommandBuffer.CreateBuffer();

    vk::DescriptorSetLayoutBinding binding[1];
    binding[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    binding[0].descriptorCount = 1;
    binding[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo info = {};
    info.bindingCount = 1;
    info.pBindings = binding;
    VulkanUtils::CheckResult(LogicalDevice.createDescriptorSetLayout(&info, nullptr, &s_RendererData.ImageDescriptorSetLayout));

    Resources::InitEngineResources();

    // Quad pipeline
    {
      auto quadShader = ShaderLibrary::CreateShaderAsync(ShaderCI{
        .VertexPath = Resources::GetResourcesPath("Shaders/Quad.vert"),
        .FragmentPath = Resources::GetResourcesPath("Shaders/Quad.frag"),
        .EntryPoint = "main",
        .Name = "Quad",
      });
      PipelineDescription quadDescription;
      quadDescription.RenderPass = s_SwapChain.m_RenderPass;
      quadDescription.VertexInputState.attributeDescriptions.clear();
      quadDescription.VertexInputState.bindingDescriptions.clear();
      quadDescription.Shader = quadShader.get();
      quadDescription.RasterizerDesc.CullMode = vk::CullModeFlagBits::eNone;
      s_Pipelines.QuadPipeline.CreateGraphicsPipelineAsync(quadDescription).wait();

      s_QuadDescriptorSet.CreateFromShader(s_Pipelines.QuadPipeline.GetShader());
    }

    // UI pipeline
    {
      auto uiShader = ShaderLibrary::CreateShaderAsync(ShaderCI{
        .VertexPath = Resources::GetResourcesPath("Shaders/UI.vert"),
        .FragmentPath = Resources::GetResourcesPath("Shaders/UI.frag"),
        .EntryPoint = "main",
        .Name = "UI",
      });
      const std::vector vertexInputBindings = {
        vk::VertexInputBindingDescription{0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex},
      };
      const std::vector vertexInputAttributes = {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat, (uint32_t)offsetof(ImDrawVert, pos)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat, (uint32_t)offsetof(ImDrawVert, uv)},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR8G8B8A8Unorm, (uint32_t)offsetof(ImDrawVert, col)},
      };
      PipelineDescription uiPipelineDecs;
      uiPipelineDecs.Name = "UI Pipeline";
      uiPipelineDecs.Shader = uiShader.get();
      uiPipelineDecs.RenderPass = s_SwapChain.m_RenderPass;
      uiPipelineDecs.VertexInputState.attributeDescriptions = vertexInputAttributes;
      uiPipelineDecs.VertexInputState.bindingDescriptions = vertexInputBindings;
      uiPipelineDecs.RasterizerDesc.CullMode = vk::CullModeFlagBits::eNone;
      uiPipelineDecs.BlendStateDesc.RenderTargets[0].BlendEnable = true;
      uiPipelineDecs.BlendStateDesc.RenderTargets[0].SrcBlend = vk::BlendFactor::eSrcAlpha;
      uiPipelineDecs.BlendStateDesc.RenderTargets[0].DestBlend = vk::BlendFactor::eOneMinusSrcAlpha;
      uiPipelineDecs.BlendStateDesc.RenderTargets[0].DestBlendAlpha = vk::BlendFactor::eOneMinusSrcAlpha;
      uiPipelineDecs.DepthDesc.DepthEnable = false;
      uiPipelineDecs.DepthDesc.DepthWriteEnable = false;
      uiPipelineDecs.DepthDesc.CompareOp = vk::CompareOp::eNever;
      uiPipelineDecs.DepthDesc.FrontFace.StencilFunc = vk::CompareOp::eNever;
      uiPipelineDecs.DepthDesc.BackFace.StencilFunc = vk::CompareOp::eNever;
      uiPipelineDecs.DepthDesc.MinDepthBound = 0;
      uiPipelineDecs.DepthDesc.MaxDepthBound = 0;
      s_Pipelines.UIPipeline.CreateGraphicsPipelineAsync(uiPipelineDecs).wait();
    }

    // Initalize default render pipeline
    if (!s_DefaultPipeline) {
      s_DefaultPipeline = CreateRef<DefaultRenderPipeline>();
      s_DefaultPipeline->OnInit();
    }

    //Create Triangle Buffers for rendering a single triangle.
    {
      std::vector<RendererData::Vertex> vertexBuffer = {
        {{-1.0f, -1.0f, 0.0f}, {}, {0.0f, 1.0f}}, {{-1.0f, 3.0f, 0.0f}, {}, {0.0f, -1.0f}},
        {{3.0f, -1.0f, 0.0f}, {}, {2.0f, 1.0f}},
      };

      VulkanBuffer vertexStaging;

      uint64_t vBufferSize = (uint32_t)vertexBuffer.size() * sizeof(RendererData::Vertex);

      vertexStaging.CreateBuffer(vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        vBufferSize,
        vertexBuffer.data());

      s_TriangleVertexBuffer.CreateBuffer(
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vBufferSize);

      SubmitOnce(CommandPoolManager::Get()->GetFreePool(),
        [&vBufferSize, &vertexStaging](const VulkanCommandBuffer copyCmd) {
          vk::BufferCopy copyRegion{};

          copyRegion.size = vBufferSize;
          vertexStaging.CopyTo(s_TriangleVertexBuffer.Get(), copyCmd.Get(), copyRegion);
        });

      vertexStaging.Destroy();
    }

    // Debug renderer
    DebugRenderer::Init();

    s_RendererConfig.ConfigChangeDispatcher.trigger(RendererConfig::ConfigChangeEvent{});

#if GPU_PROFILER_ENABLED
    //Initalize tracy profiling
    const VkPhysicalDevice physicalDevice = VulkanContext::Context.PhysicalDevice;
    TracyProfiler::InitTracyForVulkan(physicalDevice,
      LogicalDevice,
      VulkanContext::VulkanQueue.GraphicsQueue,
      s_RendererContext.m_TimelineCommandBuffer.Get());
#endif
  }

  void VulkanRenderer::Shutdown() {
    RendererConfig::Get()->SaveConfig("renderer.oxconfig");
    DebugRenderer::Release();
    s_DescriptorPoolManager->Release();
    s_CommandPoolManager->Release();
#if GPU_PROFILER_ENABLED
    TracyProfiler::DestroyContext();
#endif
  }

  void VulkanRenderer::SubmitOnce(vk::CommandPool commandPool, const std::function<void(VulkanCommandBuffer& cmdBuffer)>& submitFunc) {
    VulkanCommandBuffer cmdBuffer;
    cmdBuffer.CreateBuffer(commandPool);
    cmdBuffer.Begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    submitFunc(cmdBuffer);
    cmdBuffer.End();
    cmdBuffer.FlushBuffer();
    cmdBuffer.FreeBuffer();
    CommandPoolManager::Get()->FreePool(commandPool);
  }

  void VulkanRenderer::SetCamera(Camera& camera) {
    s_RendererContext.m_CurrentCamera = &camera;
  }

  void VulkanRenderer::Draw() {
    OX_SCOPED_ZONE;
    if (!s_RendererContext.m_CurrentCamera) {
      OX_CORE_ERROR("Renderer couldn't find a camera!");
      return;
    }

    if (!s_RendererContext.m_RenderGraph->Update(s_SwapChain, &s_SwapChain.CurrentFrame)) {
      return;
    }

    s_SwapChain.SubmitPass([](const VulkanCommandBuffer& commandBuffer) {
      OX_SCOPED_ZONE_N("Swapchain pass");
      OX_TRACE_GPU(commandBuffer.Get(), "Swapchain Pass")
      s_Pipelines.QuadPipeline.BindPipeline(commandBuffer.Get());
      s_Pipelines.QuadPipeline.BindDescriptorSets(commandBuffer.Get(), {s_QuadDescriptorSet.Get()});
      DrawFullscreenQuad(commandBuffer.Get());
      //UI pass
      Application::Get()->GetImGuiLayer()->RenderDrawData(commandBuffer.Get(), s_Pipelines.UIPipeline.Get());
    })->Submit()->Present();
  }

  void VulkanRenderer::DrawFullscreenQuad(const vk::CommandBuffer& commandBuffer, const bool bindVertex) {
    OX_SCOPED_ZONE;
    if (bindVertex) {
      constexpr vk::DeviceSize offsets[1] = {0};
      commandBuffer.bindVertexBuffers(0, s_TriangleVertexBuffer.Get(), offsets);
      commandBuffer.draw(3, 1, 0, 0);
    }
    else {
      commandBuffer.draw(3, 1, 0, 0);
    }
  }

  void VulkanRenderer::DrawIndexed(const vk::CommandBuffer& cmdBuffer,
                                   const vk::Buffer& verticiesBuffer,
                                   const vk::Buffer& indexBuffer,
                                   uint32_t indexCount) {
    OX_SCOPED_ZONE;
    constexpr vk::DeviceSize offsets[1] = {0};
    cmdBuffer.bindVertexBuffers(0, verticiesBuffer, offsets);
    cmdBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
    cmdBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
  }

  void VulkanRenderer::OnResize() {
    s_SwapChain.Resizing = true;
  }

  void VulkanRenderer::WaitDeviceIdle() {
    const auto& LogicalDevice = VulkanContext::Context.Device;
    VulkanUtils::CheckResult(LogicalDevice.waitIdle());
  }

  void VulkanRenderer::WaitGraphicsQueueIdle() {
    VulkanUtils::CheckResult(VulkanContext::VulkanQueue.GraphicsQueue.waitIdle());
  }
}
