# Windows self-hosted runner

VCStation 的原生 CI 需要一台处于登录状态的 Windows x64 工作站。常规
Debug/Release、format/lint 使用 `dvs-toolchain-4.4` 标签，D3D11VA 与五分钟性能门禁
使用 `dvs-gpu` 标签；同一台工作站可以同时拥有这两个标签。

## 本机基线（2026-07-30）

当前工作站已具备项目编译和硬件门禁所需的软件：

- Windows x64、NVIDIA GeForce RTX 4090，驱动 `32.0.16.1074`；
- Visual Studio 2022 Build Tools 与 MSVC v143；
- CMake 4.4.0、Ninja 1.13.2、Git 2.47.1；
- vcpkg 位于 `C:\Users\Sonwe\vcpkg`；
- 本地打包另有免管理员安装的 .NET SDK 8.0.423、WiX 4.0.4 与
  `WixToolset.UI.wixext` 4.0.4。

普通 build/test runner 不依赖 .NET SDK 或 WiX；只有生成 MSI 时才需要这两项。本机
原先缺少的 CI 基础设施只有 GitHub Actions runner 本体、runner 注册和九个长时素材的
稳定目录。

若重新配置机器，MSI 工具链使用以下固定版本：

```powershell
winget install --id Microsoft.DotNet.SDK.8 --exact --silent `
  --accept-package-agreements --accept-source-agreements
dotnet tool install wix --tool-path G:\GitHubActions\tools\wix --version 4.0.4
G:\GitHubActions\tools\wix\wix.exe extension add `
  --global WixToolset.UI.wixext/4.0.4
```

把 `G:\GitHubActions\tools\wix` 加入 runner 用户的 `PATH`。Release job 还要求
Windows SDK 的 `signtool.exe`，以及仓库 Secrets
`DVS_SIGNING_PFX_BASE64`、`DVS_SIGNING_PFX_PASSWORD`。MSI 是 per-machine，
因此执行 `packaged-smoke` 的交互式 runner 进程必须以管理员身份启动；测试会拒绝
覆盖机器上已有的 VCStation 安装，并在结束时卸载自己的测试安装。

## 固定下载

使用 GitHub Actions runner `2.336.0`：

```text
https://github.com/actions/runner/releases/download/v2.336.0/actions-runner-win-x64-2.336.0.zip
SHA-256 d59123a43003e357b0805b5d0f611d0bd2f65ab67d51bd070dd4e7a0f685c162
```

下载后必须先核对 SHA-256，再解压到仅供此仓库使用的目录，例如
`G:\GitHubActions\Toy-runner`。注册 token 通过仓库 API 临时生成，有效期短，不写入
仓库、脚本或日志：

```powershell
$runnerRoot = 'G:\GitHubActions\Toy-runner'
$token = gh api --method POST `
  repos/sonwe1e/Toy/actions/runners/registration-token --jq .token

Set-Location $runnerRoot
.\config.cmd --unattended `
  --url https://github.com/sonwe1e/Toy `
  --token $token `
  --name Sonwe-RTX4090 `
  --labels dvs-toolchain-4.4,dvs-gpu `
  --work _work `
  --replace
```

仓库变量保存非敏感、机器相关的固定路径。使用专用名称，避免
`ilammy/msvc-dev-cmd` 用 Visual Studio 内置 vcpkg 覆盖 `VCPKG_ROOT`：

```text
DVS_VCPKG_ROOT=C:\Users\Sonwe\vcpkg
DVS_PERFORMANCE_FIXTURE_ROOT=G:\GitHubActions\Toy-data\performance
```

`DVS_PERFORMANCE_FIXTURE_ROOT` 指向不受 checkout/clean 影响的素材目录。该目录必须
包含：

```text
gate-1080p60-a.mp4
gate-1080p60-b.mp4
gate-1080p60-c.mp4
gate-1080p120-a.mp4
gate-1080p120-b.mp4
gate-1080p120-c.mp4
gate-4k30-main10-a.mp4
gate-4k30-main10-b.mp4
gate-4k30-main10-c.mp4
```

## 运行方式与安全边界

不要把该 runner 安装成 Windows 服务。D3D11VA、Qt 渲染和可见窗口性能门禁需要当前
用户的交互桌面；服务运行在 Session 0，不能作为这组门禁的可信证据。用户登录后从
runner 目录启动 `run.cmd`，或创建“用户登录时”启动的计划任务。

此仓库是 public。self-hosted job 已限制为仓库内分支的 PR，fork PR 不会被发送到
工作站；硬件性能 workflow 仅允许维护者手动 dispatch。不要移除这些限制，也不要在
runner 上保存 GitHub token、签名证书或其他长期密钥。

runner 控制台显示 `Listening for Jobs` 后，GitHub 的 runner API 应显示：

```text
status: online
labels: self-hosted, Windows, X64, dvs-toolchain-4.4, dvs-gpu
```

常规 PR checks 全绿后，再手动运行 `Hardware and Performance`。1080p60 与 4K30
Main10 用例各运行 300 秒；2 路和 3 路 1080p120 用例各运行 60 秒。日志作为
workflow artifact 保留 14 天。
