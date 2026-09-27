# 变更记录 / CHANGELOG

## v0.3 — 现代工具条 UI（Direct2D 悬浮面板）

新增一个**现代风格的标注工具条**（`src/Toolbar.h`），把纯键盘操作升级成可视化面板：

- **外观**：底部居中圆角胶囊条，半透明深色（亚克力质感）、1px 发丝描边、柔和投影、矢量图标、悬停高亮、选中蓝底。
- **内容**：7 个工具 · 分隔 · 7 个颜色圆点（选中带白环）· 分隔 · 撤销 / 清空 / 退出。
- **零素材**：所有图标用 D2D 图元现画（含弧线几何），无图片 / 图标字体依赖。
- **交互**：点工具条即切换；命中测试优先于画笔，点按钮**不会误画**；仅在**标注态**显示（Live 态是放大镜，无 UI）。

### 新增 / 修改
- **新增** `src/Toolbar.h`、`src/Tools.h`（`Tool` 枚举抽到公共头，供 Annotator / Toolbar 共用，避免相互包含）
- **修改** `src/Annotator.h` — `Render()` 增加 `Toolbar*` 参数；暴露 `CurrentTool() / CurrentColor()`
- **修改** `src/App.h` — 集成工具条（绘制 + 命中测试 + 悬停追踪）

### 说明
- 想参考"网络 UI"未果（web 搜索这几轮一直只返回下载页），改按 **Fluent / 亚克力**设计规范实现。若你指定某个 GitHub 面板，可照它的样式重做。

---

## v0.2 — 双引擎整合（应对"活体缩放 + 冻结标注"需求）

按你最终拍板的交互，把项目从"单一冻结帧引擎"升级为**双引擎**：

| 状态 | 引擎 | 说明 |
| :--- | :--- | :--- |
| `Zooming` | **MagnifierEngine**（Windows Magnification API） | 活体放大真桌面、鼠标穿透、无自捕获反馈循环 |
| `Annotating` | **D3D11 + CAS + Direct2D** | 冻结一帧 → UV 缩放 → CAS 锐化 → 矢量标注 |

### 交互（已按你的定义实现）
- `Alt + Shift + W`：开启 / 切换 Live 运镜（全局低级键盘钩子）
- `滚轮`：Live 时平滑调倍率（1.0x ~ 5.0x）
- `左键单击`：**拦截**（不进下层 CG 软件）→ 关放大镜 → 抓帧 → 进标注
- `右键 / Esc`：退出一切 → Idle（0 占用）
- 标注态：`1`~`7` 工具、`R/G/B/Y/O/K/W` 颜色、`C` 清空、`Ctrl+Z` 撤销、`F1` 切帧率、`F2` 开关锐化

### 新增 / 修改的文件
- **新增** `src/MagnifierEngine.h` — 活体放大引擎（Mag API 封装）
- **重写** `src/App.h` — 双引擎状态机 + 低级钩子（WH_MOUSE_LL / WH_KEYBOARD_LL）
- **修改** `src/Math2D.h` — `Camera2D` 增加 `GetSourceRect()`（驱动放大镜）与 `ScreenToSource()`（标注坐标反算）
- **修改** `src/Common.h` — 链接 `dwmapi`
- **修改** `CMakeLists.txt` — 链接 `magnification`、`dwmapi`
- **沿用** `src/Renderer.h` / `src/Annotator.h` / `src/ScreenCapture.h` / `shaders/quad.hlsl`（zip 里已验证设计正确的那版）

### 修掉的问题（来自对 Gemini 双引擎代码的审查）
1. `RenderCASFrame` 没有 `Draw()`、CAS 从未编译 → 本版 Renderer 有真正的全屏三角 + 已编译的 CAS
2. 同一 HWND 上两个渲染器打架 → 本版只用 D3D11 swapchain + D2D 互操作（同一 device）
3. `WS_EX_LAYERED` + flip swapchain 不兼容 → D3D 覆盖层改用**不透明**窗口；放大镜另用 layered 穿透窗口
4. GDI 抓不到 DWM 内容 → 改用 **DXGI DD** 抓帧（`ScreenCapture`）
5. 先抓后关放大镜 → 本版**先关放大镜 + `DwmFlush()`** 再抓
6. 宽字符/ANSI 混用 → 统一 W 版 API
7. 冻结帧与放大镜视图不一致 → 冻结相机目标 = 当前视图，并用同一 `Camera2D` 做 UV 缩放

### 仍待确认 / 未解决（诚实）
- **`Alt+Shift+W` 全局被吞**：任何软件里按 Alt+Shift+W 都会触发（已从 Shift+W 改来，降低误触）。若与某个软件快捷键冲突，可再换（改 `App.h::OnKeyHook`）。
- **单显示器**：放大镜覆盖主显示器；多屏需扩展。
- **左右键取舍（已确认 ✓）**：Live 时左键 = 进标注（拦截，不进下层软件）；鼠标**移动**仍穿透。已按你的决定定稿。
- **Magnification API 画面能否被直播抓到**：仍未实测（`Win`+`+` 系统放大镜 + OBS「显示器采集」）。
- **本机无编译器**：本版仍**未编译、未运行**。
