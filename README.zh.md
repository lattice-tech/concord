<p align="center">
  <img src="Concord.png" alt="Concord" width="192">
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.zh.md">简体中文</a> | <a href="README.zh-Hant.md">繁體中文</a>
</p>

<p align="center">
  <a href="https://github.com/lattice-tech/concord/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-Apache--2.0-D22128?logo=apache&logoColor=white" alt="Apache 2.0"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" alt="C++23">
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Vulkan-0078D4" alt="Windows and Vulkan">
  <img src="https://img.shields.io/badge/Build-CMake%20%2B%20Ninja-064F8C?logo=cmake&logoColor=white" alt="CMake and Ninja">
</p>

# Concord Engine

Concord 是一款面向 Windows 的 C++23 实时 3D 引擎。公开 API 覆盖渲染、场景、输入、资产和运行时服务。

> [!WARNING]
> Concord 仍处于早期开发阶段。API、项目文件格式和运行时行为可能发生不兼容变更，构建版本也可能不稳定。

## 能力概览

| 范围 | 内容 |
|---|---|
| 渲染 | Forward+ 光照、阴影、反射、后处理、云和烟雾 |
| 运行时 | 窗口管理、场景、输入、事件和帧调度 |
| 内容 | 网格导入、材质、动画、粒子和场景序列化 |
| 工具 | 即时模式 UI、调试工具和构建支持 |

## 快速开始

1. 从 [Releases](https://github.com/lattice-tech/concord/releases) 页面下载 concord-cli。它是用于构建和管理 Concord 引擎版本的 TUI 工具。
2. 使用 concord-cli 下载所需的引擎版本。
3. 构建和集成说明请参考随引擎提供的文档。

## 使用示例

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
        .title = "我的游戏",
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

## 模块

| 模块 | 公开头文件 | 说明 |
|---|---|---|
| 动画 | `CAnimation.h` | 动画片段、混合、骨骼和状态机 |
| 应用 | `CApplication.h` | 游戏生命周期、窗口和应用配置 |
| 音频 | `CAudio.h` | 音频播放、总线、效果链、合成器与 Steam Audio HRTF 空间化 |
| 摄像机 | `CCamera.h` | 摄像机节点和描述符 |
| 角色 | `CCharacter.h` | 角色控制器和配置 |
| 碰撞 | `CCollision.h` | 碰撞形状、碰撞器和 AABB |
| 颜色 | `CColor.h` | 颜色工具 |
| 调试 | `CDebug.h` | 日志和调试叠加层 |
| ECS | `CEcs.h` | 实体、组件世界、系统和命令缓冲区 |
| 特效 | `CEffects.h` | 屏幕特效和镜头光晕描述符 |
| 环境变量 | `CEnv.h` | 全局环境值 |
| 环境 | `CEnvironment.h` | 天空、天气、昼夜和环境设置 |
| 事件 | `CEvents.h` | 类型化事件和窗口输入事件 |
| 流体 | `CFluid.h` | DFSPH 流体与 Marching-Cubes 表面重建 |
| GUI | `CGUI.h` | GUI 视窗边框与标题列样式 |
| 输入 | `CInput.h` | 键盘、鼠标和输入动作 |
| 交互 | `CInteraction.h` | UI 感知指针交互与射线反馈 |
| 光照 | `CLight.h` | 灯光和太阳光节点 |
| 材质 | `CMaterial.h` | 材质模型、表面和纹理 |
| 数学 | `CMath.h` | 向量、矩阵、四元数和欧拉角 |
| 运动 | `CMotion.h` | 缓动和节点运动 |
| 物体 | `CObject.h` | 可渲染场景节点和图元 |
| 粒子 | `CParticles.h` | 粒子发射器、力场和爆发 |
| 存档 | `CSave.h` | 场景存档系统与归档序列化 |
| 场景 | `CScene.h` | 场景所有权和序列化 |
| 烟雾 | `CSmoke.h` | 局部体积烟雾节点 |
| 系统 | `CSystem.h` | 硬件和平台信息 |
| 时间 | `CTime.h` | 时间和帧计数器 |
| UI | `CUI.h` | 即时模式 UI 和 UI 文档 |
| 工具 | `CUtils.h` | 打印和平台工具 |
| 水体 | `CWater.h` | 水体与波浪模拟 |

## 赞助

### 赞助项目

暂无。

### 赞助名单

暂无。

## 贡献流程

见 [CONTRIBUTING.md](.github/CONTRIBUTING.md)。
