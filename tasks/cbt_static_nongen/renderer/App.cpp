#include "App.hpp"

#include <tracy/Tracy.hpp>

#include "gui/ImGuiRenderer.hpp"


App::App()
  : movingOnPath(false)
{
  glm::uvec2 initialRes = {1600, 900};
  mainWindow = windowing.createWindow(
    OsWindow::CreateInfo{
      .resolution = initialRes,
    });

  renderer.reset(new Renderer(initialRes));

  auto instExts = windowing.getRequiredVulkanInstanceExtensions();
  renderer->initVulkan(instExts);

  auto surface = mainWindow->createVkSurface(etna::get_context().getInstance());

  renderer->initFrameDelivery(std::move(surface), [this]() { return mainWindow->getResolution(); });

  mainCam.lookAt({0, 20, 0}, {0, 0, -500}, {0, 1, 0});

  ImGuiRenderer::enableImGuiForWindow(mainWindow->native());

  renderer->loadScene();

  cameraPathPoints = {{0, 10, 0}, {-2000, 10, 2000}, {-2000, 10, 0}, {0, 10, 0}};
}

void App::run()
{
  double lastTime = windowing.getTime();
  while (!mainWindow->isBeingClosed())
  {
    const double currTime = windowing.getTime();
    const float diffTime = static_cast<float>(currTime - lastTime);
    lastTime = currTime;

    windowing.poll();

    processInput(diffTime);

    drawFrame(diffTime);

    FrameMark;
  }
}

void App::processInput(float dt)
{
  ZoneScoped;

  if (mainWindow->keyboard[KeyboardKey::kEscape] == ButtonState::Falling)
    mainWindow->askToClose();

  if (mainWindow->mouse[MouseButton::mbRight] == ButtonState::Rising)
    mainWindow->captureMouse = !mainWindow->captureMouse;

  if (mainWindow->keyboard[KeyboardKey::kP] == ButtonState::Falling)
  {
    movingOnPath = true;
    currentPointIndex = 0;
    mainCam.position = cameraPathPoints[currentPointIndex];
  }

  if (movingOnPath)
  {
    moveCamToPoint(mainCam, cameraPathPoints[currentPointIndex], dt);

    if (mainWindow->keyboard[KeyboardKey::k0] == ButtonState::Falling)
    {
      movingOnPath = false;
    }

    if (glm::length(cameraPathPoints[currentPointIndex] - mainCam.position) < 1e-2)
    {
      currentPointIndex++;
    }

    if (currentPointIndex == cameraPathPoints.size())
    {
      movingOnPath = false;
    }
  }
  else
  {
    if (is_held_down(mainWindow->keyboard[KeyboardKey::kLeftShift]))
      camMoveSpeed = 50;
    else
      camMoveSpeed = 2;

    moveCam(mainCam, mainWindow->keyboard, dt);

    if (mainWindow->captureMouse)
      rotateCam(mainCam, mainWindow->mouse, dt);
  }

  renderer->debugInput(mainWindow->keyboard);
}

void App::drawFrame(float dt)
{
  ZoneScoped;

  renderer->update(
    FramePacket{
      .mainCam = mainCam, .currentTime = static_cast<float>(windowing.getTime()), .deltaTime = dt});
  renderer->drawFrame();
}

void App::moveCam(Camera& cam, const Keyboard& kb, float dt)
{
  // Move position of camera based on WASD keys, and FR keys for up and down

  glm::vec3 dir = {0, 0, 0};

  if (is_held_down(kb[KeyboardKey::kS]))
    dir -= cam.forward();

  if (is_held_down(kb[KeyboardKey::kW]))
    dir += cam.forward();

  if (is_held_down(kb[KeyboardKey::kA]))
    dir -= cam.right();

  if (is_held_down(kb[KeyboardKey::kD]))
    dir += cam.right();

  if (is_held_down(kb[KeyboardKey::kF]))
    dir -= cam.up();

  if (is_held_down(kb[KeyboardKey::kR]))
    dir += cam.up();

  // NOTE: This is how you make moving diagonally not be faster than
  // in a straight line.
  cam.move(dt * camMoveSpeed * (length(dir) > 1e-9 ? normalize(dir) : dir));
}

void App::moveCamToPoint(Camera& cam, glm::vec3 point, float dt)
{
  glm::vec3 dir = point - cam.position;

  dir = (length(dir) > 1e-9 ? normalize(dir) : dir);

  cam.lookAt(cam.position, cam.position + dir - glm::vec3(0, 0.2, 0), {0, 1, 0});

  cam.move(dt * 100 * dir);
}

void App::rotateCam(Camera& cam, const Mouse& ms, float /*dt*/)
{
  // Rotate camera based on mouse movement
  cam.rotate(camRotateSpeed * ms.capturedPosDelta.y, camRotateSpeed * ms.capturedPosDelta.x);

  // Increase or decrease field of view based on mouse wheel
  cam.fov -= zoomSensitivity * ms.scrollDelta.y;
  if (cam.fov < 1.0f)
    cam.fov = 1.0f;
  if (cam.fov > 120.0f)
    cam.fov = 120.0f;
}
