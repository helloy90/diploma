#include "WorldRenderer.hpp"

#include <cstdint>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan_enums.hpp>
#include "etna/DescriptorSet.hpp"
#include "etna/Etna.hpp"
#include "imgui.h"

#include "shaders/Light.h"
#include "shaders/DirectionalLight.h"


WorldRenderer::WorldRenderer()
  : terrainMgr{std::make_unique<TerrainManager>(11, 511)}
  , renderTargetFormat(vk::Format::eB10G11R11UfloatPack32)
  , maxNumberOfSamples(16)
  , wireframeEnabled(false)
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "main_view_depth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
    });

  renderTarget = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "render_target",
      .format = renderTargetFormat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
    });

  generationParamsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(
      etna::Buffer::CreateInfo{
        .size = sizeof(TerrainGenerationParams),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO,
        .allocationCreate =
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .name = fmt::format("generationConstants{}", i)});
  });

  gBuffer.emplace(resolution, renderTargetFormat);

  params.terrainInChunks = shader_uvec2(64, 64);
  params.chunk = shader_uvec2(16, 16);
  params.terrainOffset =
    -static_cast<glm::vec2>(params.terrainInChunks * params.chunk) / glm::vec2(2);

  constantsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(
      etna::Buffer::CreateInfo{
        .size = sizeof(UniformParams),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO,
        .allocationCreate =
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .name = fmt::format("constants{}", i)});
  });

  terrainSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eMirroredRepeat,
      .name = "terrain_sampler"});

  oneShotCommands = ctx.createOneShotCmdMgr();
}

void WorldRenderer::loadScene()
{
  terrainMgr->loadTerrain();

  params.instancesCount = terrainMgr->getInstanceMeshes().size();
  params.relemsCount = terrainMgr->getRenderElements().size();
}

void WorldRenderer::loadShaders()
{

  etna::create_program(
    "terrain_generator",
    {DEFERRED_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "generator.frag.spv"});

  etna::create_program("culling_meshes", {DEFERRED_RENDERER_SHADERS_ROOT "culling.comp.spv"});

  etna::create_program(
    "terrain_normal_map_calculation", {DEFERRED_RENDERER_SHADERS_ROOT "calculate_normal.comp.spv"});

  etna::create_program(
    "terrain_tesselation_render",
    {DEFERRED_RENDERER_SHADERS_ROOT "chunk.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "process_chunk.tese.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "terrain.frag.spv"});

  etna::create_program(
    "terrain_render",
    {DEFERRED_RENDERER_SHADERS_ROOT "clipmap.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "clipmap.frag.spv"});

  etna::create_program(
    "lights_displacement", {DEFERRED_RENDERER_SHADERS_ROOT "displace_lights.comp.spv"});

  etna::create_program(
    "deferred_shading",
    {DEFERRED_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     DEFERRED_RENDERER_SHADERS_ROOT "shading.frag.spv"});
}

void WorldRenderer::setupRenderPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = terrainMgr->getVertexFormatDescription(),
    }},
  };

  terrainRenderPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_render",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframeEnabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eClockwise,
          .lineWidth = 1.f,
        },
      .blendingConfig =
        {
          .attachments =
            {{
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {renderTargetFormat, vk::Format::eR8G8B8A8Snorm},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  terrainTesselationRenderPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_tesselation_render",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframeEnabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .blendingConfig =
        {
          .attachments =
            {{
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {renderTargetFormat, vk::Format::eR8G8B8A8Snorm},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  deferredShadingPipeline = pipelineManager.createGraphicsPipeline(
    "deferred_shading",
    etna::GraphicsPipeline::CreateInfo{
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {renderTargetFormat},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  cullingPipeline = pipelineManager.createComputePipeline("culling_meshes", {});
}

void WorldRenderer::rebuildRenderPipelines()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getQueue().waitIdle());

  // TODO: fix sync error in queue submit
  setupRenderPipelines();
}

void WorldRenderer::setupTerrainGeneration(vk::Format texture_format, vk::Extent3D extent)
{
  auto& ctx = etna::get_context();
  auto& pipelineManager = ctx.getPipelineManager();

  terrainMap = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = extent,
      .name = "terrain_map",
      .format = texture_format,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
        vk::ImageUsageFlagBits::eStorage});
  terrainNormalMap = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = extent,
      .name = "terrain_normal_map",
      .format = vk::Format::eR8G8B8A8Snorm,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});

  terrainGenerationPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_generator",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {texture_format},
      }});

  terrainNormalPipeline =
    pipelineManager.createComputePipeline("terrain_normal_map_calculation", {});

  lightDisplacementPipeline = pipelineManager.createComputePipeline("lights_displacement", {});

  params.extent = shader_uvec2(extent.width, extent.height);
  params.heightAmplifier = 400.0f;
  params.heightOffset = 0.6f;
  generationParams = {.extent = params.extent, .numberOfSamples = 3, .persistence = 0.5};
}

void WorldRenderer::loadLights()
{
  auto& ctx = etna::get_context();

  params.constant = 1.0f;
  params.linear = 0.14f;
  params.quadratic = 0.07f;

  std::array lights = {
    Light{.pos = {0, 5, 25}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 5},
    Light{.pos = {3, 5, 50}, .radius = 0, .worldPos = {}, .color = {0.5, 1, 0.5}, .intensity = 5},
    Light{.pos = {75, 5, 75}, .radius = 0, .worldPos = {}, .color = {1, 0.5, 1}, .intensity = 5},
    Light{.pos = {50, 5, 20}, .radius = 0, .worldPos = {}, .color = {0, 1, 1}, .intensity = 5},
    Light{.pos = {25, 5, 50}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 5},
    Light{.pos = {50, 5, 50}, .radius = 0, .worldPos = {}, .color = {0.3, 1, 0}, .intensity = 5},
    Light{.pos = {25, 5, 10}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 5},
    Light{
      .pos = {100, 5, 100}, .radius = 0, .worldPos = {}, .color = {1, 0.5, 0.5}, .intensity = 5},
    Light{.pos = {150, 5, 150}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 10},
    Light{.pos = {25, 5, 10}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 5},
    Light{.pos = {10, 5, 25}, .radius = 0, .worldPos = {}, .color = {1, 0, 1}, .intensity = 5},
    Light{.pos = {0, 5, 0}, .radius = 0, .worldPos = {}, .color = {1, 0, 1}, .intensity = 5}};

  for (auto& light : lights)
  {
    float lightMax = glm::max(light.color.r, light.color.g, light.color.b);
    light.radius = (-params.linear +
                    static_cast<float>(glm::sqrt(
                      params.linear * params.linear -
                      4 * params.quadratic * (params.constant - (256.0 / 5.0) * lightMax)))) /
      (2 * params.quadratic);
    // spdlog::info("radius - {}", light.radius);
  }

  std::array directionalLights = {DirectionalLight{
    .direction = glm::normalize(glm::vec3{-1, 1, 0}),
    .intensity = 1.0f,
    .color = glm::normalize(glm::vec3{251, 172, 19})}};

  vk::DeviceSize directionalLightsSize = sizeof(DirectionalLight) * directionalLights.size();
  vk::DeviceSize lightsSize = sizeof(Light) * lights.size();

  directionalLightsBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = directionalLightsSize,
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = fmt::format("DirectionalLights")});
  lightsBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = lightsSize,
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = fmt::format("Lights")});

  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = std::max(directionalLightsSize, lightsSize)});

  transferHelper->uploadBuffer(
    *oneShotCommands, directionalLightsBuffer, 0, std::as_bytes(std::span(directionalLights)));
  transferHelper->uploadBuffer(*oneShotCommands, lightsBuffer, 0, std::as_bytes(std::span(lights)));

  params.directionalLightsAmount = static_cast<uint32_t>(directionalLights.size());
  params.lightsAmount = static_cast<uint32_t>(lights.size());
}


void WorldRenderer::debugInput(const Keyboard& keyboard)
{
  if (keyboard[KeyboardKey::kF3] == ButtonState::Falling)
  {
    wireframeEnabled = !wireframeEnabled;

    rebuildRenderPipelines();
  }
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    params.view = packet.mainCam.viewTm();
    params.invView = glm::inverse(params.view);
    params.proj = packet.mainCam.projTm(aspect);
    params.invProj = glm::inverse(params.proj);
    params.projView = params.proj * params.view;
    params.invProjView = glm::inverse(params.projView);
    params.invProjViewMat3 = glm::mat4x4(glm::inverse(glm::mat3x3(params.projView)));
    params.cameraWorldPosition = packet.mainCam.position;
    // spdlog::info("camera position - {}, {}, {}", params.cameraWorldPosition.x,
    // params.cameraWorldPosition.y, params.cameraWorldPosition.z);

    terrainMgr->moveClipmap(params.cameraWorldPosition);
  }
}

void WorldRenderer::drawGui()
{
  static ImU32 numberOfSamplesMin = 1;
  static float persistenceMin = 0.0f;
  static float persistenceMax = 1.0f;

  ImGui::Begin("Render Settings");

  ImGui::Text(
    "Application average %.3f ms/frame (%.1f FPS)",
    1000.0f / ImGui::GetIO().Framerate,
    ImGui::GetIO().Framerate);

  if (ImGui::CollapsingHeader("Terrain Generation"))
  {
    ImGui::SeparatorText("Generation parameters");
    ImGui::SliderScalar(
      "Number of samples",
      ImGuiDataType_U32,
      &generationParams.numberOfSamples,
      &numberOfSamplesMin,
      &maxNumberOfSamples,
      "%u");
    ImGui::SliderScalar(
      "Persistence",
      ImGuiDataType_Float,
      &generationParams.persistence,
      &persistenceMin,
      &persistenceMax,
      "%f");
    if (ImGui::Button("Regenerate Terrain"))
    {
      generateTerrain();
    }
  }

  if (ImGui::CollapsingHeader("World Render Settings"))
  {
    if (ImGui::Checkbox("Enable Wireframe Mode", &wireframeEnabled))
    {
      rebuildRenderPipelines();
    }
  }

  ImGui::End();
}

void WorldRenderer::generateTerrain()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());

  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    auto& currentGenerationConstants = generationParamsBuffer->get();
    currentGenerationConstants.map();
    std::memcpy(
      currentGenerationConstants.data(), &generationParams, sizeof(TerrainGenerationParams));
    currentGenerationConstants.unmap();

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    auto extent = terrainMap.getExtent();
    glm::uvec2 glmExtent = {extent.width, extent.height};

    {
      etna::RenderTargetState state(
        commandBuffer,
        {{}, {glmExtent.x, glmExtent.y}},
        {{terrainMap.get(), terrainMap.getView({})}},
        {});

      auto shaderInfo = etna::get_shader_program("terrain_generator");
      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, currentGenerationConstants.genBinding()}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        terrainGenerationPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);


      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, terrainGenerationPipeline.getVkPipeline());

      commandBuffer.draw(3, 1, 0, 0);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      auto shaderInfo = etna::get_shader_program("terrain_normal_map_calculation");

      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{
           1, terrainNormalMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        terrainNormalPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, terrainNormalPipeline.getVkPipeline());

      commandBuffer.pushConstants<glm::uvec2>(
        terrainNormalPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0,
        {params.chunk});

      commandBuffer.dispatch((glmExtent.x + 31) / 32, (glmExtent.y + 31) / 32, 1);
    }
    {
      auto shaderInfo = etna::get_shader_program("lights_displacement");

      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, currentConstants.genBinding()},
         etna::Binding{1, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{2, lightsBuffer.genBinding()}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        lightDisplacementPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, lightDisplacementPipeline.getVkPipeline());

      commandBuffer.dispatch(1, 1, 1);
    }

    {
      std::array bufferBarriers = {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = lightsBuffer.get(),
        .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eVertexShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eVertexShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WorldRenderer::cullTerrain(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = terrainMgr->getDrawInstanceIndicesBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = terrainMgr->getDrawCommandsBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  auto shaderInfo = etna::get_shader_program("culling_meshes");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, terrainMgr->getRelemsBuffer().genBinding()},
     etna::Binding{1, terrainMgr->getBoundsBuffer().genBinding()},
     etna::Binding{2, terrainMgr->getMeshesBuffer().genBinding()},
     etna::Binding{3, terrainMgr->getInstanceMeshesBuffer().genBinding()},
     etna::Binding{4, terrainMgr->getInstanceMatricesBuffer().genBinding()},
     etna::Binding{5, terrainMgr->getRelemInstanceOffsetsBuffer().genBinding()},
     etna::Binding{6, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
     etna::Binding{7, terrainMgr->getDrawCommandsBuffer().genBinding()},
     etna::Binding{8, constants.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.dispatch((terrainMgr->getInstanceMeshes().size() + 127) / 128, 1, 1);

  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = terrainMgr->getDrawInstanceIndicesBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .buffer = terrainMgr->getDrawCommandsBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }
}

void WorldRenderer::renderTesselationTerrain(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("terrain_tesselation_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, constants.genBinding()},
     etna::Binding{
       1, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{
       2,
       terrainNormalMap.genBinding(
         terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.draw(4, params.terrainInChunks.x * params.terrainInChunks.y, 0, 0);
}

void WorldRenderer::renderTerrain(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  if (!terrainMgr->getVertexBuffer())
  {
    return;
  }

  cmd_buf.bindVertexBuffers(0, {terrainMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(terrainMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  auto shaderInfo = etna::get_shader_program("terrain_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, terrainMgr->getInstanceMatricesBuffer().genBinding()},
      etna::Binding{1, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
      etna::Binding{2, constants.genBinding()},
      etna::Binding{
        3, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{
        4,
        terrainNormalMap.genBinding(terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.drawIndexedIndirect(
    terrainMgr->getDrawCommandsBuffer().get(),
    0,
    terrainMgr->getRenderElements().size(),
    sizeof(vk::DrawIndexedIndirectCommand));
}

void WorldRenderer::deferredShading(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("deferred_shading");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, constants.genBinding()},
     gBuffer->genAlbedoBinding(1),
     gBuffer->genNormalBinding(2),
     gBuffer->genDepthBinding(3),
     etna::Binding{4, lightsBuffer.genBinding()},
     etna::Binding{5, directionalLightsBuffer.genBinding()},
     etna::Binding{
       6, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::uvec2), &resolution);

  cmd_buf.draw(3, 1, 0, 0);
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    {
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());
      cullTerrain(cmd_buf, currentConstants, cullingPipeline.getVkPipelineLayout());
    }

    gBuffer->prepareForRender(cmd_buf);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        gBuffer->genColorAttachmentParams(),
        gBuffer->genDepthAttachmentParams());

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainRenderPipeline.getVkPipeline());
      renderTerrain(cmd_buf, currentConstants, terrainRenderPipeline.getVkPipelineLayout());
    }

    // {
    //   ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
    //   etna::RenderTargetState renderTargets(
    //     cmd_buf,
    //     {{0, 0}, {resolution.x, resolution.y}},
    //     gBuffer->genColorAttachmentParams(vk::AttachmentLoadOp::eLoad),
    //     gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad));

    //   cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
    //   terrainTesselationRenderPipeline.getVkPipeline()); renderTesselationTerrain(cmd_buf,
    //   currentConstants, terrainTesselationRenderPipeline.getVkPipelineLayout());
    // }

    gBuffer->prepareForRead(cmd_buf);

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredShading);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = renderTarget.get(), .view = renderTarget.getView({})}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      cmd_buf.bindPipeline(
        vk::PipelineBindPoint::eGraphics, deferredShadingPipeline.getVkPipeline());
      deferredShading(cmd_buf, currentConstants, deferredShadingPipeline.getVkPipelineLayout());
    }

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferRead,
      vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      target_image,
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    {
      std::array srcOffset = {
        vk::Offset3D{},
        vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1}};
      auto srdImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1};

      std::array dstOffset = {
        vk::Offset3D{},
        vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1}};
      auto dstImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1};

      auto imageBlit = vk::ImageBlit2{
        .sType = vk::StructureType::eImageBlit2,
        .pNext = nullptr,
        .srcSubresource = srdImageSubrecourceLayers,
        .srcOffsets = srcOffset,
        .dstSubresource = dstImageSubrecourceLayers,
        .dstOffsets = dstOffset};

      auto blitInfo = vk::BlitImageInfo2{
        .sType = vk::StructureType::eBlitImageInfo2,
        .pNext = nullptr,
        .srcImage = renderTarget.get(),
        .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
        .dstImage = target_image,
        .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        .regionCount = 1,
        .pRegions = &imageBlit,
        .filter = vk::Filter::eLinear};

      cmd_buf.blitImage2(&blitInfo);
    }
  }
}

void WorldRenderer::updateConstants(etna::Buffer& constants)
{
  ZoneScoped;

  constants.map();
  std::memcpy(constants.data(), &params, sizeof(UniformParams));
  constants.unmap();
}
