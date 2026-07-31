# VCStation（VideoCompareStation）

VCStation 是面向 Windows 的 2～3 路逐帧视频审查工具，使用 C++20、Qt Quick、
FFmpeg 动态库和 D3D11。它把 Reference 与一到两路预测视频放在同一个 canonical
frame position 上，支持精确逐帧、任意两路 Wipe/Diff、显式对齐以及缺帧和重复帧诊断。

当前产品方案以 [USERPLAN.md](USERPLAN.md) 为准。`legacy/` 只保存历史实现作为行为参考，
不参与构建。

---

## 主要工作流

启动后可直接拖入 2～3 个视频；三路素材会先确认 A/B/C 顺序和 Reference。也可以拖入
`.dvsproj` 或从 File 菜单打开评测项目。项目文件保存视频路径、Reference、Offset、
锚点和视图设置，不会导出新视频。

常用审查能力包括：

- Side by side 使用明确白色分割线，Wipe compare 可拖动分割线并比较 A/B、A/C 或 B/C；
- A/D 或左右方向键逐 1 帧，Shift+A/D 逐 5 帧，Ctrl+A/D 按 canonical FPS 跳 1 秒；
- 连续逐帧期间保留旧画面并接受新请求，最终只呈现最新请求，不用全屏 Loading 遮挡；
- Advanced Alignment Inspector 默认收起，需要时再估计整体偏移、分析缺帧/重复帧或设置手动锚点；
- File → Export Bad Case 会把当前对比图和包含 frame/source/alignment 身份的
  `evidence.json` 原子发布到一个独立目录。

用户包不携带外部 `ffmpeg.exe` 或 `ffprobe.exe`。GUI 与
`VCStationCli --probe` 都直接使用随包分发的 FFmpeg 动态库。

---

## 构建与验证

要求 Windows x64、Visual Studio 2022 Build Tools/MSVC v143、CMake 4.4、Ninja 和
vcpkg。首次配置会根据 `vcpkg.json` 安装 Qt 6.11、FFmpeg 8.1.2、GoogleTest 与
nlohmann-json。所有生成物都在未跟踪的 `out/` 下。

从 Visual Studio x64 开发者 PowerShell 运行：

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target lint
```

启动应用与 CLI 诊断：

```powershell
.\out\build\dev\bin\VCStation.exe
.\out\build\dev\bin\VCStationCli.exe --startup-check
.\out\build\dev\bin\VCStationCli.exe --probe .\video.mp4
```

确定性的 WARP UI smoke：

```powershell
.\out\build\dev\bin\VCStation.exe --ui-smoke `
  .\tests\fixtures\media\h264_a_320x180_30fps_12.mp4 `
  .\tests\fixtures\media\h264_b_160x90_30fps_12.mp4
```

Release 与安装包：

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cpack --preset release-zip
cpack --preset release-msi
ctest --preset shutdown-soak --output-on-failure
ctest --preset packaged-smoke --output-on-failure
```

MSI 生成还要求 .NET SDK 8、WiX 4.0.4 与 `WixToolset.UI.wixext` 4.0.4；
`packaged-smoke` 会真实安装 per-machine MSI，因此 runner 必须提升权限且不能已有
VCStation 安装。详细配置见
[docs/self-hosted-runner.md](docs/self-hosted-runner.md)。

---

## 性能与发布门禁

硬件门禁必须在登录到交互桌面的 D3D11VA runner 上运行，不能用 WARP 或 CPU 测试替代。
`performance-d3d11` 包括三路 1080p60 与 4K30 Main10 五分钟门禁，以及两路/三路
1080p120 各 60 秒门禁；检查 presentation ACK 连续性、source atomicity、完整
FrameSet drop、帧预算、冷 seek P95、相邻逐帧和有界关闭。

```powershell
$env:DVS_PERFORMANCE_FIXTURE_ROOT = 'G:\GitHubActions\Toy-data\performance'
ctest --preset hardware-d3d11 --output-on-failure
ctest --preset performance-d3d11 --output-on-failure
```

标签触发的 Release workflow 在测试后签名 `VCStation.exe`、`VCStationCli.exe` 和 MSI，
再执行真实安装与 shutdown soak 门禁。签名证书缺失时发布会失败关闭，不会生成未签名的
正式 Release。

---

## 代码结构

`src/domain` 只包含规则，`src/application` 只包含用例和 ports；
`platform_windows`、`media_ffmpeg`、`persistence_json` 与 `ui_qml` 是外层适配器，
`src/app` 负责组合。测试按层放在 `tests/`，打包定义在 `packaging/`，品牌资源在
`assets/branding/`。架构与贡献约束见
[docs/architecture.md](docs/architecture.md) 和 [AGENTS.md](AGENTS.md)。
