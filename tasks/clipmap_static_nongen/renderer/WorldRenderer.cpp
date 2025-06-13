#include "WorldRenderer.hpp"

#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <stb_image.h>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Sampler.hpp>

#include "render_utils/Utilities.hpp"


WorldRenderer::WorldRenderer()
  : lightModule()
  , terrainRenderModule()
  , freezeClipmap(false)
  , renderTargetFormat(vk::Format::eB10G11R11UfloatPack32)
  , wireframeEnabled(false)
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  renderTarget = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "render_target",
      .format = renderTargetFormat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
    });

  gBuffer.emplace(resolution, renderTargetFormat);

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

  cubemapSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "cubemapSampler"});
  heightMapSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "heightMapSampler"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 6});

  lightModule.allocateResources();
  terrainRenderModule.allocateResources();

  heightMapTexture = render_utility::load_texture(
    *transferHelper,
    *oneShotCommands,
    GRAPHICS_COURSE_RESOURCES_ROOT "/textures/HeightMaps/4K/Heightmap_06_Canyons_blurred.png",
    vk::Format::eR8G8B8A8Srgb);

  info = {.extent = glm::ivec2(4096), .heightOffset = 0.04f, .heightAmplifier = 10000.0f};

  terrainInfoBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(TerrainInfo),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "terrainInfo"});

  terrainInfoBuffer.map();
  std::memcpy(terrainInfoBuffer.data(), &info, sizeof(TerrainInfo));
  terrainInfoBuffer.unmap();
}

// call only after loadShaders(...)
void WorldRenderer::loadScene()
{
  terrainRenderModule.loadMaps(
    {etna::Binding{
       0,
       heightMapTexture.genBinding(
         heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{1, terrainInfoBuffer.genBinding()}});

  lightModule.loadLights(
    {{.pos = {0, 27, 0}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 15},
     {.pos = {0, 5, 0}, .radius = 0, .worldPos = {}, .color = {1, 0, 1}, .intensity = 15},
     {.pos = {0, 5, 25}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 15},
     {.pos = {3, 5, 50}, .radius = 0, .worldPos = {}, .color = {0.5, 1, 0.5}, .intensity = 15},
     {.pos = {75, 5, 75}, .radius = 0, .worldPos = {}, .color = {1, 0.5, 1}, .intensity = 15},
     {.pos = {50, 5, 20}, .radius = 0, .worldPos = {}, .color = {0, 1, 1}, .intensity = 15},
     {.pos = {25, 5, 50}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 15},
     {.pos = {50, 5, 50}, .radius = 0, .worldPos = {}, .color = {0.3, 1, 0}, .intensity = 15},
     {.pos = {25, 5, 10}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 15},
     {.pos = {100, 5, 100}, .radius = 0, .worldPos = {}, .color = {1, 0.5, 0.5}, .intensity = 15},
     {.pos = {150, 5, 150}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 100},
     {.pos = {25, 5, 10}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 15},
     {.pos = {10, 5, 25}, .radius = 0, .worldPos = {}, .color = {1, 0, 1}, .intensity = 15}},
    {{.direction = glm::vec3{1, -0.35, -3},
      .intensity = 1.0f,
      .color = glm::vec3{1, 0.694, 0.32}}});

  lightModule.loadMaps(
    {etna::Binding{
       0,
       heightMapTexture.genBinding(
         heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{1, terrainInfoBuffer.genBinding()}});

  lightModule.displaceLights();
}

void WorldRenderer::loadShaders()
{
  lightModule.loadShaders();
  terrainRenderModule.loadShaders();

  etna::create_program(
    "deferred_shading",
    {PROJECT_RENDERER_STATIC_NONGEN_SHADERS_ROOT "decoy.vert.spv",
     PROJECT_RENDERER_STATIC_NONGEN_SHADERS_ROOT "shading.frag.spv"});
}

void WorldRenderer::setupRenderPipelines()
{
  lightModule.setupPipelines();
  terrainRenderModule.setupPipelines(wireframeEnabled, renderTargetFormat);

  auto& pipelineManager = etna::get_context().getPipelineManager();

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
}

void WorldRenderer::rebuildRenderPipelines()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getQueue().waitIdle());

  setupRenderPipelines();
}

void WorldRenderer::loadCubemap()
{
  const uint32_t layerCount = 6;
  std::string path = GRAPHICS_COURSE_RESOURCES_ROOT "/textures/Cubemaps/Sea/";
  std::vector<std::string> filenames = {
    path + "nz.png",
    path + "pz.png",
    path + "py.png",
    path + "ny.png",
    path + "px.png",
    path + "nx.png",
  };

  if (filenames.size() != layerCount)
  {
    ETNA_PANIC("Amount of textures is not equal to amount of image layers!");
  }

  unsigned char* textures[layerCount];
  int width, height, channels;
  for (uint32_t i = 0; i < layerCount; i++)
  {
    textures[i] = stbi_load(filenames[i].c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (textures[i] == nullptr)
    {
      ETNA_PANIC("Texture {} is not loaded!", filenames[i].c_str());
    }
  }

  uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

  const vk::DeviceSize cubemapSize = width * height * 4 * layerCount;
  const vk::DeviceSize layerSize = cubemapSize / layerCount;

  etna::Buffer cubemapBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = cubemapSize,
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc |
        vk::BufferUsageFlagBits::eTransferDst,
      .name = "cubemap_buffer",
    });

  for (uint32_t i = 0; i < layerCount; i++)
  {
    auto source = std::span<unsigned char>(textures[i], layerSize);
    transferHelper->uploadBuffer(
      *oneShotCommands, cubemapBuffer, static_cast<uint32_t>(layerSize * i), std::as_bytes(source));
  }

  cubemapTexture = etna::get_context().createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
      .name = "cubemap_image",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eTransferSrc,
      .layers = layerCount,
      .mipLevels = mipLevels,
      .flags = vk::ImageCreateFlagBits::eCubeCompatible});

  render_utility::local_copy_buffer_to_image(
    *oneShotCommands, cubemapBuffer, cubemapTexture, layerCount);

  render_utility::generate_mipmaps_vk_style(
    *oneShotCommands, cubemapTexture, mipLevels, layerCount);

  for (int i = 0; i < 6; i++)
  {
    stbi_image_free(textures[i]);
  }
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
    renderPacket = {
      .projView = params.projView,
      .view = params.view,
      .proj = params.proj,
      .cameraWorldPosition = params.cameraWorldPosition,
      .time = packet.currentTime};

    if (!freezeClipmap)
    {
      terrainRenderModule.update(renderPacket);
    }
  }
}

void WorldRenderer::drawGui()
{
  ImGui::Begin("Application Settings");

  ImGui::Text(
    "Application average %.3f ms/frame (%.1f FPS)",
    1000.0f / ImGui::GetIO().Framerate,
    ImGui::GetIO().Framerate);

  ImGui::Text(
    "Camera World Position - x:%f ,y:%f ,z:%f",
    params.cameraWorldPosition.x,
    params.cameraWorldPosition.y,
    params.cameraWorldPosition.z);

  ImGui::SeparatorText("Specific Settings");

  lightModule.drawGui();

  static bool infosChanged = false;

  if (ImGui::CollapsingHeader("Terrain settings"))
  {
    ImGui::SeparatorText("Terrain info");
    int extent[] = {info.extent.x, info.extent.y};
    infosChanged = infosChanged || ImGui::DragInt2("Extent", extent, 1, 1, 131072);
    float heightOffset = info.heightOffset;
    infosChanged =
      infosChanged || ImGui::DragFloat("Height Offset", &heightOffset, 0.01f, -10.0f, 10.0f, "%f");
    float heightAmplifier = info.heightAmplifier;
    infosChanged = infosChanged ||
      ImGui::DragFloat("Height Amplifier", &heightAmplifier, 0.01f, 1.0f, 1024.0f, "%f");

    info = {
      .extent = {extent[0], extent[1]},
      .heightOffset = heightOffset,
      .heightAmplifier = heightAmplifier,
    };
  }

  if (infosChanged)
  {
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
    terrainInfoBuffer.map();
    std::memcpy(terrainInfoBuffer.data(), &info, sizeof(TerrainInfo));
    terrainInfoBuffer.unmap();
    infosChanged = false;
  }

  terrainRenderModule.drawGui();

  ImGui::SeparatorText("General Settings");


  if (ImGui::Checkbox("Enable Wireframe Mode", &wireframeEnabled))
  {
    rebuildRenderPipelines();
  }

  ImGui::Checkbox("Freeze Clipmap", &freezeClipmap);

  ImGui::End();
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
     gBuffer->genMaterialBinding(3),
     gBuffer->genDepthBinding(4),
     etna::Binding{5, lightModule.getPointLightsBuffer().genBinding()},
     etna::Binding{6, lightModule.getDirectionalLightsBuffer().genBinding()},
     etna::Binding{7, lightModule.getLightParamsBuffer().genBinding()},
     etna::Binding{
       8,
       cubemapTexture.genBinding(
         cubemapSampler.get(),
         vk::ImageLayout::eShaderReadOnlyOptimal,
         {.type = vk::ImageViewType::eCube})}});

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
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    auto& currentConstants = constantsBuffer->get();
    currentConstants.map();
    std::memcpy(currentConstants.data(), &params, sizeof(UniformParams));
    currentConstants.unmap();
    gBuffer->prepareForRender(cmd_buf);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, fullTerrainRender);
      terrainRenderModule.execute(
        cmd_buf,
        renderPacket,
        resolution,
        gBuffer->genColorAttachmentParams(),
        gBuffer->genDepthAttachmentParams());
    }

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    gBuffer->prepareForRead(cmd_buf);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredShading);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = renderTarget.get(), .view = renderTarget.getView({})}},
        {});

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

    render_utility::blit_image(
      cmd_buf,
      renderTarget.get(),
      target_image,
      vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1});
  }
}
