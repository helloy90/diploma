#include "WorldRenderer.hpp"

#include <glm/fwd.hpp>
#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <stb_image.h>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Sampler.hpp>

#include "Light/LightModule.hpp"
#include "etna/DescriptorSet.hpp"
#include "etna/Etna.hpp"
#include "render_utils/Utilities.hpp"


WorldRenderer::WorldRenderer()
  : lightModule()
  , waterGeneratorModule()
  , waterRenderModule()
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

  oneShotCommands = ctx.createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 6});

  lightModule.allocateResources();
  waterGeneratorModule.allocateResources();
  waterRenderModule.allocateResources();
}

// call only after loadShaders(...)
void WorldRenderer::loadScene()
{
  lightModule.loadLights(
    {{.pos = {0, 27, 0}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 15}},
    {{.direction = glm::vec3{1, -0.35, -3},
      .intensity = 1.0f,
      .color = glm::vec3{1, 0.694, 0.32}}});
  waterGeneratorModule.executeStart();
}

void WorldRenderer::loadShaders()
{
  lightModule.loadShaders();
  waterGeneratorModule.loadShaders();
  waterRenderModule.loadShaders();

  etna::create_program(
    "cubemap_render",
    {PROJECT_RENDERER_WATER_SHADERS_ROOT "cubemap.vert.spv",
     PROJECT_RENDERER_WATER_SHADERS_ROOT "cubemap.frag.spv"});
}

void WorldRenderer::setupRenderPipelines()
{
  lightModule.setupPipelines();
  waterGeneratorModule.setupPipelines();
  waterRenderModule.setupPipelines(wireframeEnabled, renderTargetFormat);

  cubemapPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "cubemap_render",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {renderTargetFormat},
        .depthAttachmentFormat = vk::Format::eD32Sfloat,
      }});
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
      waterRenderModule.update(renderPacket);
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

  waterGeneratorModule.drawGui();
  waterRenderModule.drawGui();

  ImGui::SeparatorText("General Settings");

  if (ImGui::Checkbox("Enable Wireframe Mode", &wireframeEnabled))
  {
    rebuildRenderPipelines();
  }

  ImGui::Checkbox("Freeze Clipmap", &freezeClipmap);

  ImGui::End();
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image)
{

  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  auto& currentConstants = constantsBuffer->get();
  currentConstants.map();
  std::memcpy(currentConstants.data(), &params, sizeof(UniformParams));
  currentConstants.unmap();

  waterGeneratorModule.executeProgress(cmd_buf, renderPacket.time);

  etna::set_state(
    cmd_buf,
    renderTarget.get(),
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageAspectFlagBits::eColor);


  etna::set_state(
    cmd_buf,
    waterGeneratorModule.getHeightMap().get(),
    vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    vk::ImageAspectFlagBits::eColor);

  etna::set_state(
    cmd_buf,
    waterGeneratorModule.getNormalMap().get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    vk::ImageAspectFlagBits::eColor);

  gBuffer->prepareForRender(cmd_buf);

  etna::flush_barriers(cmd_buf);

  waterRenderModule.execute(
    cmd_buf,
    renderPacket,
    resolution,
    {{.image = renderTarget.get(), .view = renderTarget.getView({})}},
    gBuffer->genDepthAttachmentParams(),
    waterGeneratorModule.getHeightMap(),
    waterGeneratorModule.getNormalMap(),
    waterGeneratorModule.getSampler(),
    lightModule.getDirectionalLightsBuffer(),
    cubemapTexture);

  {
    ETNA_PROFILE_GPU(cmd_buf, renderCubemap);
    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = renderTarget.get(),
        .view = renderTarget.getView({}),
        .loadOp = vk::AttachmentLoadOp::eLoad}},
      gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad));

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, cubemapPipeline.getVkPipeline());
    renderCubemap(cmd_buf, cubemapPipeline.getVkPipelineLayout());
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

void WorldRenderer::renderCubemap(vk::CommandBuffer cmd_buf, vk::PipelineLayout layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("cubemap_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
      0,
      cubemapTexture.genBinding(
        cubemapSampler.get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        {.type = vk::ImageViewType::eCube})}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {vkSet}, {});

  struct PushConstant
  {
    glm::mat4 invProjView;
    glm::uvec2 res;
  } pushConst{.invProjView = params.invProjViewMat3, .res = resolution};

  cmd_buf.pushConstants<PushConstant>(layout, vk::ShaderStageFlagBits::eFragment, 0, {pushConst});

  cmd_buf.draw(3, 1, 0, 0);
}
