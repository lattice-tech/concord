<p align="center">
  <img src="Concord.png" alt="Concord" width="192">
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.zh.md">简体中文</a>
</p>

<p align="center">
  <a href="https://github.com/lattice-tech/concord/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-Apache--2.0-D22128?logo=apache&logoColor=white" alt="Apache 2.0"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" alt="C++23">
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Vulkan-0078D4" alt="Windows and Vulkan">
  <img src="https://img.shields.io/badge/Build-CMake%20%2B%20Ninja-064F8C?logo=cmake&logoColor=white" alt="CMake and Ninja">
</p>

# Concord Engine

Concord is a Windows-focused C++23 engine for real-time 3D applications. Its public API covers rendering, scenes, input, assets, and runtime services.

> [!WARNING]
> Concord is in early development. APIs, project file formats, and runtime behavior may change without compatibility guarantees, and builds may be unstable.

## Capabilities

| Area | Includes |
|---|---|
| Rendering | Forward+ lighting, shadows, reflections, post-processing, clouds, and smoke |
| Runtime | Window management, scenes, input, events, and frame scheduling |
| Content | Mesh import, materials, animation, particles, and scene serialization |
| Tooling | Immediate-mode UI, debugging utilities, and build support |

## Quick Start

1. Download [concord-cli](https://github.com/lattice-tech/concord/releases) from the Releases page. It is a TUI tool for building and managing Concord engine versions.
2. Use concord-cli to download the engine version you need.
3. Refer to the documentation shipped with the engine for build and integration details.

## Usage

```cpp
#include <Concord/CApplication.h>
#include <Concord/CCamera.h>
#include <Concord/CLight.h>
#include <Concord/CObject.h>
#include <Concord/CScene.h>

int main()
{
    Concord::Game game;
    Concord::Window window({
        .title = "My Game",
        .resolution = {.width = 1280, .height = 720},
        .resizable = true,
    });
    game.AttachWindow(window);

    Concord::Scene scene;
    scene.Spawn<Concord::Object::Camera>(Concord::Object::CameraDesc{
        .position = {0.0f, 2.0f, -5.0f},
        .target = {0.0f, 0.0f, 0.0f},
    });
    scene.Spawn<Concord::Object::SunLight>(Concord::Object::SunLightDesc{
        .localSolarTimeHours = 12.0f,
        .latitudeDegrees = 45.0f,
        .year = 2026,
        .month = 7,
        .day = 21,
    });
    scene.Spawn<Concord::Object::Box>(Concord::Object::BoxDesc{
        .transform = {.position = {0.0f, 1.0f, 0.0f}},
    });

    game.LoadScene(scene);
    game.Run();
}
```

## Modules

| Module | Public header | Description |
|---|---|---|
| Animation | `CAnimation.h` | Animation clips, blending, skeletons, and state machines |
| Application | `CApplication.h` | Game lifecycle, windows, and application configuration |
| Audio | `CAudio.h` | Runtime stereo audio facade and Steam Audio HRTF spatialization |
| Camera | `CCamera.h` | Camera nodes and descriptors |
| Character | `CCharacter.h` | Character controller and configuration |
| Collision | `CCollision.h` | Collision shapes, colliders, and AABBs |
| Color | `CColor.h` | Color utilities |
| Debug | `CDebug.h` | Logging and debug overlay facilities |
| ECS | `CEcs.h` | Entities, component world, systems, and command buffers |
| Effects | `CEffects.h` | Screen effects and lens flare descriptors |
| Environment Variables | `CEnv.h` | Global environment values |
| Environment | `CEnvironment.h` | Sky, weather, day-night, and environment settings |
| Events | `CEvents.h` | Typed events and window input events |
| Input | `CInput.h` | Keyboard, mouse, and input actions |
| Interaction | `CInteraction.h` | UI-aware pointer interaction and raycast feedback |
| Lighting | `CLight.h` | Light and sunlight nodes |
| Materials | `CMaterial.h` | Material models, surfaces, and textures |
| Math | `CMath.h` | Vectors, matrices, quaternions, and Euler angles |
| Motion | `CMotion.h` | Easing and node motion |
| Objects | `CObject.h` | Renderable scene nodes and primitives |
| Particles | `CParticles.h` | Particle emitters, force fields, and bursts |
| Scene | `CScene.h` | Scene ownership and serialization |
| Smoke | `CSmoke.h` | Local volumetric smoke nodes |
| System | `CSystem.h` | Hardware and platform information |
| Time | `CTime.h` | Time and frame counters |
| UI | `CUI.h` | Immediate-mode UI and UI documents |
| Utilities | `CUtils.h` | Printing and platform utilities |

## Sponsorship

### Sponsored Projects

None yet.

### Sponsors

None yet.

## Contribution Process

See [CONTRIBUTING.md](.github/CONTRIBUTING.md).
