# ZoomCraft

**屏幕平滑缩放 + 矢量标注工具** —— 面向直播演示 / 教学 / 录屏。

一句话：按 `Alt+Shift+W`，屏幕像摄像机一样**平滑跟随鼠标放大**；左键一按**冻结画面**，切成**锐化 + 画笔标注**；右键或 `Esc` 退出。

---

## 它解决什么

- 直播 / 演示时要放大屏幕某处、并**当场圈画讲解**；
- 既要 `obs-zoom-to-mouse` 那种**丝滑缓动跟随**，又要 `ZoomIt` 的**标注**；
- 放大后文字 / 边缘要**清晰**（CAS 锐化），不要放大 3 倍就糊成马赛克；
- **体积小、占用低** —— 纯 GPU 管线，无 Electron、无 AI 超分（Real-ESRGAN 之类会吃爆显存）。

## 核心特性

- **双引擎**：活体放大（Windows Magnification API）+ 冻结锐化标注（D3D11 + Direct2D）
- **临界阻尼弹簧相机**（与 obs-zoom-to-mouse 同款算法）→ 镜头带惯性、丝滑
- **CAS 对比度自适应锐化**：抵消双线性放大后的模糊
- **矢量标注**：笔 / 直线 / 箭头 / 矩形 / 椭圆 / 荧光笔 / 文字（含 Undo）
- **现代悬浮工具条**：底部圆角胶囊、半透明深色、矢量图标、悬停/选中态
- **帧率闸**：60 / 90 / 120 / 无上限
- **零外部运行时依赖**（HLSL 运行时编译，源码内嵌）

## 工作原理（双引擎状态机）

```
Idle ──(Alt+Shift+W)──▶ Zooming ──(左键)──▶ Annotating
                          │                    │
                          └────(右键 / Esc)─────┴──▶ Idle
```

- **Zooming（活体放大）**：用 **Magnification API** 放大真桌面 —— 画面是**活的**、鼠标**穿透**（移动照给下层软件）、且由 DWM 托管所以**永不抓到自己**。弹簧相机每帧跟随鼠标，滚轮调速。
- **Annotating（冻结标注）**：关放大镜 → `DwmFlush()` 等合成 → **DXGI Desktop Duplication** 抓一帧 → **D3D11** 渲染冻结帧（UV 缩放）+ **CAS** 锐化 → **Direct2D** 叠标注与工具条。

### 三个关键设计（为什么这么做）

1. **Live 用放大镜、不自己抓屏渲染** —— 自己抓整屏又自己显示，会"放大图里套放大图"无限递归（DD 抓的是合成后的整张桌面，且 DD 1.2 无法排除自身窗口）。放大镜由 DWM 托管，天然无此问题。
2. **冻结帧抓屏用 DXGI DD，不用 GDI** —— DD 是 VRAM→VRAM 拷贝，能抓到 GPU 合成内容；GDI BitBlt 常抓成黑屏。
3. **标注坐标存"源图像素空间"** —— 每帧按当前相机变换到屏幕，缩放/平移时标注像"贴"在内容上，不会飘。

## 快捷键

| 键 | 作用 |
| :--- | :--- |
| `Alt+Shift+W` | **全局**：开启 / 切换 Live 缩放 |
| `滚轮` | Live 时平滑调倍率（1.0x ~ 5.0x） |
| `左键` | Live：进入标注（拦截，不进下层软件）；标注态：绘制 |
| `右键` / `Esc` | 退出一切 → Idle（0 占用） |
| `1`..`7` | 工具：笔 / 直线 / 箭头 / 矩形 / 椭圆 / 荧光笔 / 文字 |
| `R G B Y O W K` | 颜色：红 / 绿 / 蓝 / 黄 / 橙 / 白 / 黑 |
| `C` | 清空标注 |
| `Ctrl+Z` | 撤销上一笔 |
| `F1` / `F2` | 切帧率档 / 开关锐化 |
| `Enter` | 提交文字（文字工具下） |

> 以上工具 / 颜色 / 撤销 / 清空 / 退出也可**直接用鼠标点底部工具条**。

## 构建（需要 Visual Studio 2022 + Windows SDK 10 + CMake ≥ 3.21）

```bat
:: 在 "x64 Native Tools Command Prompt for VS 2022" 里：
cd ZoomCraft
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
:: 产物: build\Release\ZoomCraft.exe
```

运行时**无外部文件依赖**（HLSL 内嵌在 `Renderer.h`，运行时由 `d3dcompiler_47` 编译）。

## 文件结构

```
ZoomCraft/
  CMakeLists.txt
  README.md            # 本文件
  CHANGELOG.md         # 变更记录
  MERGED_DESIGN.md     # 方案合并总纲（含与 Gemini 方案的对照）
  SUMMARY_CN.md        # 早期总结（历史）
  shaders/quad.hlsl    # 着色器参考拷贝
  src/
    Common.h           # 头文件 / 库链接 / 日志
    Math2D.h           # SmoothDamp 弹簧 + Camera2D（含屏幕↔源坐标换算）
    FrameLimiter.h     # 60/90/120/无上限
    MagnifierEngine.h  # 活体放大引擎（Magnification API）
    ScreenCapture.h    # DXGI DD 抓帧 + GDI 回落
    Renderer.h         # D3D11 swapchain + 全屏三角 + CAS（内嵌 HLSL）
    Annotator.h        # Direct2D + DirectWrite 矢量标注
    Toolbar.h          # 现代悬浮工具条 UI
    Tools.h            # Tool 枚举（公共）
    App.h              # 双引擎状态机 + 低级钩子 + 主循环
    main.cpp           # wWinMain
```

## 当前状态 / 已知限制

- ⚠️ **尚未在本机编译运行**（这台机器没装 C++ 编译器）。首次构建可能有个别小报错。
- **单显示器**：放大镜只覆盖主屏，多屏需扩展。
- **Live 时左键被拦截**去进标注 → 此时**无法用左键操作下层软件**（只有鼠标移动穿透）。这是既定设计。
- ⚠️ **直播能否抓到放大画面**：待实测（系统放大镜 + OBS「显示器采集」，**别用**窗口/游戏采集）。
- `Present(0,0)` 不做垂直同步，动画时可能撕裂；要绝对无撕裂可改 `Present(1,0)`（但会锁显示器刷新率）。
