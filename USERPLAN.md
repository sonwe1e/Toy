# 核心结论

我检查的 1.2.0 基线为 `259e012f`。当前 `feature/vcstation-userplan`、`main` 和 `v1.2.0` 标签处于同一代码基线。项目已经完成去 Project 化、Player-first 界面、1～3 路统一会话、Wipe/Diff、时间轴、Alignment、Explorer 右键和性能门禁，整体工程完成度较高。

但 1.2.0 仍不能视为完全成熟的稳定版。它当前最主要的问题已经不是基础播放或 Renderer，而集中在以下五类：

| 维度        | 当前判断                             |
| --------- | -------------------------------- |
| 核心播放与比较   | 基本成熟                             |
| UI 主体结构   | 方向正确，仍有状态和响应式缺陷                  |
| Alignment | 能用，但搜索范围和长视频扩展性不足                |
| 媒体兼容性     | 更接近静音视觉审查器，不是通用播放器               |
| 发布安全      | 无 Authenticode 签名，是最高发布风险        |
| 架构整洁度     | 去 Project 后仍残留 Workspace 薄壳和重复状态 |
| 发布证据      | 当前连接器未显示可用于证明该精确 SHA 的完整状态结果     |

综合评价：

> **VCStation 1.2.0 是一个能力较完整的专业视觉审查版本，但更适合作为受控发布或 Release Candidate；要成为稳定公开版本，还需要完成发布安全、状态一致性和扩展性加固。**

---

# 一、必须优先解决的 P0 问题

## 1. 无签名发布，尤其不适合直接分发 Shell Extension

Release workflow 明确为 `v1.2.0` 设置了无签名例外，最终只生成 ZIP、MSI 和 SHA-256，并没有执行 Authenticode 签名。Release Notes 也明确提示会出现 Unknown Publisher 或 SmartScreen 警告。

普通便携 EXE 无签名已经会影响信任；当前 MSI 还安装了一个由 Windows Explorer 加载的原生 COM DLL：

```text
VCStationShell-1.2.dll
```

这比普通应用无签名更敏感。企业机器、安全软件或 Windows 策略可能直接阻止安装或加载。

### 技术路线

正式稳定版恢复完整签名链：

```text
Release build
→ 签名 VCStation.exe
→ 签名 VCStationCli.exe
→ 签名 VCStationShell-1.2.dll
→ 生成 ZIP/MSI
→ 签名 MSI
→ 验证所有签名和时间戳
→ 对签名后的包执行 packaged smoke
→ 生成 SHA256SUMS
```

建议证书先导入临时证书存储，通过 thumbprint 签名，避免将 PFX 密码暴露在 `signtool /p` 命令行。

签名暂时无法恢复时，应采用更安全的发布分层：

```text
VCStation 1.2.0 Preview ZIP
    不安装 Shell Extension
    明确标记 unsigned preview

VCStation Stable MSI
    必须签名
    包含 Explorer Shell Extension
```

SHA-256 只能验证下载完整性，不能替代发布者身份认证。

---

## 2. Prediction-only 模式的语义是矛盾的

确认框允许选择：

```text
None (prediction-only)
```

它会把 Reference 索引设置为 `-1`，所有视频都被标记为 Prediction。Shell 和 ReviewController 已经允许这个输入。

但进入主界面后：

* 系统仍然必须选择一个 canonical timeline source；
* Source A 通常被当作 canonical source；
* Source Strip 会把 canonical source 显示为 `R`；
* Compare 菜单中的 Reference 只能选择 A/B/C；
* 用户无法再切回 `None`；
* Reference Focus 仍会使用第一个 Source 作为 Reference panel。

因此用户选择“没有 Reference”，主界面却又显示 A 为 Reference。

### 推荐路线

必须拆分两个概念：

```cpp
struct ReviewSourceRoles {
    SourceId canonicalSourceId;           // 必须存在，决定时间轴
    std::optional<SourceId> referenceId;  // 可以为空，表示没有 GT
};
```

确认框改成：

```text
Timeline source
    Video A / Video B / Video C

Ground-truth role
    Same as timeline source
    Video A / Video B / Video C
    None
```

UI 标识也应拆开：

```text
T = Timeline owner
R = Reference / Ground truth
```

只有一个角色时可以合并显示 `T/R`。

若暂时不准备实现两个角色，1.2.x 最稳妥的做法是删除 `None (prediction-only)`，要求始终选择一个 Reference，避免错误表达。

---

## 3. Busy 状态下，本地打开请求可能静默丢失

外部单实例请求已有排队逻辑，但本地操作没有统一进入同一个队列：

* File → Open videos 没有 `!busy` 限制；
* `Ctrl+O` 始终可执行；
* DropArea 始终接受拖入；
* 多视频确认框在某些 busy 场景仍可打开；
* `ReviewController::openSources()` 在已有 pending command 时直接返回 `false`；
* 上层多个调用点没有检查返回值并显示错误。

实际体验可能是：

```text
正在打开视频 A/B
→ 用户再次拖入视频 C
→ 确认
→ 后端拒绝新请求
→ 界面没有明显反馈
```

### 技术路线

在 `ReviewShellController` 中建立统一的用户意图队列：

```cpp
enum class ReviewIntentKind {
    OpenVideos,
    AddVideo,
    ReplaceSources,
    ChangeReference,
    CloseVideos
};

struct ReviewIntent {
    ReviewIntentKind kind;
    QList<QUrl> sources;
    int referenceIndex;
};
```

所有来源都进入同一入口：

```text
File menu
Drag and drop
Explorer request
Command line
Source + button
Reference change
```

策略建议：

* Open/Close/Change Reference：串行执行；
* 新的 Open Videos 可以覆盖尚未执行的旧 Open Videos；
* 正在显示确认框时，其余请求排队；
* 请求被拒绝时显示非模态 Toast；
* busy 时菜单可以继续点击，但必须明确显示“请求已排队”，不能静默丢弃。

---

## 4. 1.1.0 升级后旧 `.dvsproj` 注册清理没有被证明

1.1.0 安装包曾注册：

* `HKLM\Software\Classes\.dvsproj`
* `VCStation.Project`
* `Applications\VCStation.exe\SupportedTypes\.dvsproj`

1.2.0 已经从新安装定义中删除这些组件，但当前升级测试不再检查这些旧注册项是否在 1.1.0→1.2.0 后消失。

通常 Major Upgrade 会卸载旧组件，但发布门禁不能依赖“理论上会清理”。如果残留，用户双击旧 `.dvsproj` 时，Windows 仍可能启动 VCStation，而新版本已不支持该参数。

### 技术路线

在当前 WiX 中增加防御性清理：

```xml
<RemoveRegistryKey
    Root="HKLM"
    Key="Software\Classes\.dvsproj"
    Action="removeOnInstall"/>

<RemoveRegistryKey
    Root="HKLM"
    Key="Software\Classes\VCStation.Project"
    Action="removeOnInstall"/>
```

同时处理：

```text
Software\Classes\Applications\VCStation.exe\SupportedTypes\.dvsproj
```

升级测试必须验证：

```text
安装 1.1.0
→ 确认 .dvsproj 注册存在
→ 升级 1.2.0
→ 确认所有旧注册消失
→ 卸载 1.2.0
→ 再次确认没有残留
```

---

## 5. 文档和真实发布行为互相矛盾

当前至少有两处明显不一致：

* Release Notes 仍写着单视频支持 “save and restore”，但 Project 已完全删除；
* README 声称 Release workflow 会签名 GUI、CLI、Shell DLL 和 MSI，且缺少证书时失败关闭；实际 workflow 明确为 1.2.0 开启无签名例外。

README 还声称“快捷键方案会自动保存”，但当前 `shortcutPreset` 只是 `Main.qml` 的临时属性，没有进入 Settings。

### 技术路线

建立一个发布一致性测试，检查文档中的关键声明：

```text
Project support
Signing mode
Supported startup parameters
Supported extensions
Performance matrix
Active version
```

Release Notes 不应手工复制这些信息。可以由 CMake 生成一份：

```json
{
  "version": "1.2.0",
  "signed": false,
  "projectFiles": false,
  "shellExtension": true,
  "audioPlayback": false
}
```

再由文档和发布 workflow 共同引用。

---

# 二、界面和使用体验问题

## 1. 空状态下 Compare 和 Analyze 仍然可以打开

当前判断为：

```qml
enabled: !root.singleMode
```

空状态时 `sourceCount == 0`，因此 `singleMode == false`，Compare 和 Analyze 菜单会被错误启用。用户可以在没有视频时修改 Wipe、Diff、Analysis Grid 等设置。

应改成：

```qml
enabled: root.sourceCount > 1 && !root.busy
```

Analyze 还应增加：

```qml
enabled:
    root.sourceCount > 1
    && root.graphicsReady
    && root.currentFrame >= 0
```

空状态下右键菜单同样会显示多个无意义的禁用项，应在 `sourceCount == 0` 时只保留：

```text
Open videos…
Full screen
```

---

## 2. “Project” 已删除，但用户可见的 “Review” 概念仍然过多

现在界面仍包含：

* Loading review；
* Drop to review；
* current review；
* Review timeline；
* Review tab；
* Review preset。

对开发者而言，Review Session 是合理内部术语；但用户要求的是“视频”概念，用户界面不应再要求理解另一种会话对象。

建议统一文案：

| 当前                           | 推荐                     |
| ---------------------------- | ---------------------- |
| Loading review               | Loading videos         |
| Close review                 | Close videos           |
| Add source to current review | Add video              |
| Review timeline              | Video timeline         |
| Review markers               | Analysis markers       |
| Review preset                | Frame review shortcuts |

内部类名可以继续使用 Review，用户可见文本不必暴露。

---

## 3. 单视频模式仍固定占用一条 Source Strip

单视频时顶部仍显示：

```text
A · video.mp4   SOURCE   +
```

它占用 42 px。对比较模式有价值，但对单视频播放器仍显得偏工程化。

建议单视频采用浮动 Source Chip：

```text
左上角：
A · video.mp4   [+]
```

鼠标静止后降低透明度；加入第二个视频后再切换为固定 Active Source Strip。

这样单视频默认 Viewer 可以获得完整高度。

---

## 4. Auto-hide OSC 的触发区域不够自然

OSC Auto 模式隐藏后，只能通过底部 10 px 的 wake area 重新显示。鼠标移动到视频中央不会唤醒控制器。

成熟播放器通常采用：

```text
鼠标在 Viewer 中明显移动
→ 显示 OSC
→ 1.2～2 秒无操作后隐藏
```

### 技术路线

在 Viewport 层记录鼠标移动时间：

```qml
PointerHandler {
    onPointChanged: oscController.reveal()
}
```

OSC 状态机：

```cpp
enum class OscVisibility {
    Pinned,
    Revealed,
    Fading,
    Hidden
};
```

在以下行为发生时强制显示：

* 鼠标移动；
* 播放/暂停；
* seek；
* 设置 In/Out；
* 切换 Reference；
* 发生错误。

底部 wake area 可保留为补充入口，而不是唯一入口。

---

## 5. 顶层菜单栏仍没有完全自定义

`VcsMenu` 和 `VcsMenuItem` 已经自定义了 Popup 和行样式，但 `VcsMenuBar` 只定义背景和底边，顶层的 File、Compare、Analyze、View 仍依赖默认 `MenuBarItem`。

在不同 DPI、Basic Style 或键盘焦点下，顶层 hover/focus 仍可能与 Popup 不一致。

应补充：

```qml
delegate: MenuBarItem {
    contentItem: Text { ... }
    background: Rectangle {
        color: highlighted ? accent : transparent
    }
}
```

同时测试：

* Alt 键激活；
* Left/Right 切换顶层菜单；
* 高 DPI；
* 中文长菜单标题；
* 菜单靠近屏幕右边缘时自动翻转子菜单。

---

## 6. Context Menu 状态反馈不足

当前 Viewer 右键菜单中：

* View 下的 Side/Wipe/Diff 不显示当前 checked 状态；
* Pair 在两路视频时仍可见但禁用；
* Reference 在单视频或空状态时仍可见；
* Full screen 不会变成 Exit full screen。

建议：

```text
两路：
    View ✓
    Inspector
    Export
    Full screen

三路：
    View ✓
    Pair ✓
    Reference ✓
    Inspector
    Export
    Full screen
```

禁用的整组功能优先隐藏，而不是展示大量灰色项。

---

## 7. 单视频仍然没有音频

当前应用 ports、媒体能力文档和运行链路只有视频 probe、FrameProvider、Alignment、Render 和 Settings，没有音频解码、重采样、输出或 A/V clock。

因此 1.2.0 的准确定位是：

> **静音视频播放与逐帧视觉审查工具。**

但 UI 使用 “play video” 等普通播放器语言，会让用户期待声音。

### 1.2.x 建议

不要临时加入不成熟音频链路，而是在空状态、Info 页和 README 明确：

```text
Visual playback only · Audio is not played
```

### 后续完整路线

```text
FFmpeg audio stream selection
→ decoder
→ swresample
→ WASAPI shared-mode output
→ audio ring buffer
→ A/V clock strategy
→ seek flush
→ pause/resume
→ source switching
```

多视频比较时还需要明确只播放 canonical source 音频，或者允许静音所有音频。

---

# 三、媒体兼容性不足

## 1. 编码与 HDR 支持范围较窄

当前正式支持：

* H.264；
* HEVC；
* MPEG-4 Part 2；
* SDR；
* 部分 YUV/RGB 输入归一化。

PQ 和 HLG 会被拒绝，AV1、VP9 等现代编码也不在当前合同内。

这对于游戏录屏、浏览器视频和现代移动设备素材存在明显限制。

### 技术路线

按优先级扩展：

```text
第一阶段
    VP9 + WebM
    AV1 software decode
    .ts/.m2ts 常见 H.264/HEVC 容器

第二阶段
    D3D11VA / D3D12 Video AV1 capability probe
    失败时软件 fallback

第三阶段
    PQ/HLG 解码
    线性光转换
    可配置 tone mapping
    Windows Advanced Color / HDR output
```

HDR Diff 必须明确比较空间：

```text
Source-linear difference
Display-referred difference
Tone-mapped visual difference
```

不能把 tone-mapped 图像差异称为 pixel-exact。

---

## 2. Explorer 入口与应用能力不完全一致

VCStation 支持 1～3 路视频，但 Explorer Shell Extension 主要面向恰好两路选择。

建议最终提供动态命令：

```text
选中 1 个：
    Open in VCStation

选中 2 个：
    Compare with VCStation

选中 3 个：
    Compare 3 videos with VCStation
```

另外，GUI 启动解析目前只支持：

```text
--play video
--compare video...
```

直接传递裸路径会报 unsupported arguments。

应支持 Windows 常见调用：

```text
VCStation.exe video.mp4
VCStation.exe a.mp4 b.mp4
VCStation.exe a.mp4 b.mp4 c.mp4
```

这样拖文件到 EXE、Open With 和其他工具调用才自然。

---

# 四、Alignment 能力的限制

## 1. 全局 Offset 只搜索 ±16 帧

`GlobalOffsetEstimationOptions` 默认范围为：

```cpp
minimumOffset = -16;
maximumOffset = 16;
```

UI 手工 Offset 也限制在 `[-16, 16]`。

在 60 FPS 视频中只有约 ±267 ms，在 120 FPS 中只有约 ±133 ms。两个录制起点相差一秒就无法自动找到。

### 技术路线：分层搜索

```text
阶段 1：粗搜索
    每 0.5～1 秒采样一次
    感知哈希/低分辨率特征
    搜索 ±10～60 秒

阶段 2：细搜索
    在最佳粗 Offset 附近
    按帧搜索 ±1～2 秒

阶段 3：局部校正
    使用 banded sequence alignment
    处理缺帧、重复帧和局部漂移
```

UI 不应只输入帧数，应支持：

```text
Search range: ±10 seconds
Manual offset: +00:00:01.250 / +75 frames
```

---

## 2. Sequence Alignment 上限为 50,000 帧

Sequence Alignment Request 的最大帧数为 50,000。

对应：

```text
30 FPS ≈ 27.8 分钟
60 FPS ≈ 13.9 分钟
120 FPS ≈ 6.9 分钟
```

较长的游戏视频会无法完整分析。

### 技术路线：分块层级 Alignment

```text
Scene boundary / coarse anchor detection
→ 2～5 分钟分块
→ 相邻块保留重叠区
→ 每块进行 banded alignment
→ 用单调约束拼接映射
→ 对冲突区域标为 Review Required
```

内存继续保持：

```text
O(chunk_size × band_width)
```

而不是构建全视频 N×M 矩阵。

---

## 3. Timeline Marker 最多只投影 256 个

ReviewController 中存在固定的最大 Timeline Marker 数量：

```cpp
kMaximumAlignmentTimelineMarkers = 256
```

超出的异常不会继续进入 UI。

长视频或大量丢帧时，用户可能以为只有前 256 个问题。

### 技术路线

不要直接把全部异常放进 QML，改用两级数据：

```text
全局 Timeline：
    按像素/时间 Bucket 聚合
    显示密度与严重度

局部查询：
    用户缩放或点击区域
    请求该时间范围内的详细异常
```

新增接口：

```cpp
QVariantList markerBuckets(MediaTime start, MediaTime end, int bucketCount);
QVariantList markerDetails(MediaTime start, MediaTime end, int limit);
```

界面必须显示：

```text
1,284 anomalies · timeline is aggregated
```

不能静默截断。

---

## 4. 去 Project 时连 Alignment Cache 一并删除

Project 持久化被删除是正确的，但原来的 Derived Alignment Cache 也一起删除。这意味着用户反复打开相同视频并重新执行完整序列分析时，需要重新解码和计算。

缓存不应依赖 Project，应该独立存在。

### 技术路线

建立 content-addressed cache：

```text
Cache key =
    source file identity/fingerprint
    + canonical source identity
    + algorithm version
    + alignment options
    + timing metadata version
```

缓存内容：

```text
低分辨率签名
粗 Offset
Sequence segments
异常摘要
```

缓存失效条件：

* 文件大小或修改时间变化；
* 快速指纹变化；
* 算法版本变化；
* Alignment 参数变化。

它只是性能缓存，不恢复 UI 会话，也不会重新引入 Project 概念。

---

# 五、架构与代码维护问题

## 1. Workspace 层已经退化为多余薄壳

去 Project 后，`WorkspaceCoordinator` 主要只负责：

* 转发 Playback 命令；
* 记录一个 displayName；
* Close Review；
* 转发 terminal。

但它仍保留：

* `acceptedSequenceAlignments` 依赖；
* `SourceRevalidationDiagnostics`；
* `WorkspaceSnapshot`；
* `WorkspaceController`；
* 未使用的 `ReviewPreferencesController` 注入。

这会造成：

```text
PlaybackCoordinator
→ WorkspaceCoordinator
→ WorkspaceController
→ Main.qml
```

仅为了关闭当前视频。

### 技术路线

优先方案：

```text
ReviewController
├── openSources
├── changeReference
├── closeSources
├── playback commands
└── terminal projection
```

删除：

```text
WorkspaceCoordinator
WorkspaceController
WorkspaceSnapshot
SourceRevalidationDiagnostics
```

另一种方案是将其重命名为 `ReviewSessionLifecycle`，只保留明确的 Open/Close 生命周期，不再暴露旧 Workspace 语义。

---

## 2. Main.qml 仍保存了过多业务状态

虽然已拆出多个组件，`Main.qml` 仍持有：

* `selectedSourceA/B/C`；
* `pendingDroppedVideos`；
* staged source；
* startup request；
* In/Out；
* range loop；
  -菜单；
  -快捷键；
  -对话框；
  -打开/关闭状态机。

同时 `ReviewShellController` 又维护另一套 staged/active sources。

部分 `sourceA/B/C FileDialog` 已经没有明显可见入口，隐藏的 `openPairButton` 也仍然留在 Main 中。

### 技术路线

把业务状态完全收进 C++：

```text
ReviewSessionFacade
├── activeSources
├── stagedSources
├── pendingIntent
├── canonicalSource
├── referenceSource
├── rangeState
└── startupQueue
```

QML 拆为：

```text
Main.qml
ApplicationMenuBar.qml
ReviewShortcuts.qml
ReviewInputDialogs.qml
ReviewWorkspace.qml
```

Main.qml 最终只负责布局与信号连接。

---

## 3. QML 子组件仍使用 `required property var host`

`ComparisonViewport`、`TabbedInspector` 等组件仍能访问 Main 的所有属性和函数。

问题包括：

* 依赖关系不透明；
* qmllint 难以完整检查；
* 测试需要模拟整个 Main；
* 一个 Main 属性变更可能影响多个组件。

应逐步改为显式接口：

```qml
TabbedInspector {
    sourceCount: session.sourceCount
    currentMode: preferences.viewMode
    mediaInfo: controller.sourceMediaInfo

    onReferenceRequested:
        session.changeReference(sourceId)
}
```

---

## 4. ROI/Pan 命中仍重复实现 Renderer 布局

Source 标签已经使用 `sourcePanelRects`，但 `panelPoint()` 仍然在 Main.qml 中手工判断：

* Side-by-side；

* Three-up；

* Reference Focus；

* Analysis Grid。

这意味着 Renderer 布局、标签布局和鼠标命中仍有两套计算。

### 技术路线

由 `ComparisonSurface` 暴露：

```cpp
Q_INVOKABLE QVariantMap mapSurfacePoint(qreal x, qreal y);
```

返回：

```json
{
  "panelIndex": 1,
  "sourceSlot": 2,
  "normalizedX": 0.42,
  "normalizedY": 0.63
}
```

ROI、Pan、Zoom 和 Label 全部使用同一 `SurfacePanelLayout`。

---

## 5. Settings 持久化与文档不一致

当前 Settings 实际保存：

* View mode；
* Diff metric/gain/edge/filter；
* OSC mode；
* largeStepFrames。

但没有保存：

* Shortcut preset；
* DF/NDF preference。

`largeStepFrames` 仍允许 5/10，但当前快捷键已经固定为 5 帧，这个设置基本成为死配置。

Preferences Controller 还使用 16 ms Timer 轮询 Settings completion，在空闲状态下造成不必要的持续唤醒。

### 技术路线

* 删除 `largeStepFrames`；
* 增加 `shortcutPreset`；
* 增加 `dropFrameTimecode`；
* 将 Settings repository completion 改为 event-driven queued callback；
* 增加 settings schema migration；
* 更新 README 与测试。

---

# 六、Bad Case 导出的不足

当前 Bad Case 导出：

* 使用未压缩 BMP；
* 在 Viewer grab 完成回调中同步写磁盘；
* evidence.json 只包含 Source、Frame、Alignment 身份；
* 不包含当前 View/Wipe/Diff/ROI 等呈现状态。

结果是：用户看到的是一个 Diff 或 Wipe Bad Case，但 evidence 无法完整说明如何复现它。

### 技术路线

升级为 schema v2：

```json
{
  "schema_version": 2,
  "app_version": "1.2.x",
  "media_time_us": 1234567,
  "canonical_frame": 100,
  "view": {
    "mode": "wipe",
    "pair": [0, 1],
    "wipe_position": 0.37,
    "difference_metric": "luma",
    "difference_gain": 4,
    "threshold": 0.08,
    "roi": {}
  },
  "sources": []
}
```

其他修改：

* `comparison.bmp` 改为 `comparison.png`；
* 图像编码和文件写入移到低优先级 worker；
* 显示导出进度；
* 退出时若导出尚未提交，明确取消或等待；
* 可选保存 Source 快速指纹，不默认保存绝对路径。

---

# 七、性能门禁仍有空缺

当前矩阵为：

```text
1×1080p60
3×1080p60
1×1080p120
2×1080p120
3×1080p120
```

没有最常见的：

```text
2×1080p60
```

三路 1080p60 能证明更高负载，但不能完全替代两路 Side/Wipe/Diff 的实际路径门禁。

建议增加：

```text
performance.1080p60-2source
```

至少覆盖：

* Side-by-side；
* Wipe；
* Diff；
* seek；
* 连续逐帧；
* Reference 切换；
* source rebuild；
* 关闭。

另外，ASAN 和 Coverage preset 虽然存在，但当前标准 Build/Quality workflow 没有运行 ASAN，也没有对真实代码执行覆盖率门禁。

建议：

```text
每次 PR：
    Debug / Release / lint

定期或关键 PR：
    ASAN

主线夜间：
    Coverage
    shutdown soak
    long alignment
```

---

# 八、建议版本路线

## 1.2.1：发布与正确性补丁

只处理高风险问题：

1. 恢复签名，或将无签名包降级为 Preview；
2. 修复空状态 Compare/Analyze；
3. 统一 busy 请求队列和错误反馈；
4. 解决 prediction-only 语义；
5. 验证并强制清理旧 `.dvsproj` 注册；
6. 修正文档与真实行为；
7. 持久化 Shortcut preset 和 DF/NDF；
8. 增加两路 1080p60 门禁。

## 1.3.0：架构与体验收口

1. 删除或重构 Workspace 薄壳；
2. 建立 `ReviewSessionFacade`；
3. 删除 QML 重复 staged 状态；
4. 拆分 Main.qml；
5. 统一 Surface hit-test；
6. 完成响应式 Inspector；
7. 支持裸视频路径启动和 1/2/3 文件 Explorer 命令；
8. 改进 OSC 鼠标唤醒；
9. Bad Case schema v2。

## 1.4.0：媒体和长视频能力

1. 独立 Alignment Cache；
2. 分层大范围 Offset 搜索；
3. 分块 Sequence Alignment；
4. 聚合 Timeline Marker；
5. VP9/AV1；
6. HDR/tone mapping；
7. 视产品定位决定是否实现完整音频。

---

# 九、最终验收标准

1. 精确发布 SHA 的 Debug、Release、Quality、Hardware、Performance 和 Packaged Smoke 均有可追溯证据。
2. 所有稳定 MSI/EXE/Shell DLL 都通过 Authenticode 验证。
3. 1.1.0 升级后没有任何 `.dvsproj` 注册残留。
4. 空状态不能操作 Compare 或 Analyze。
5. busy 状态下打开请求不会静默丢失。
6. Prediction-only 在确认框、Source Strip、菜单、Inspector 和 Renderer 中语义一致。
7. 单视频 UI 明确说明是否支持音频。
8. 两路 1080p60 有独立性能门禁。
9. 相差数秒的视频可以自动估计 Offset。
10. 超过 50,000 帧的视频可以分块分析。
11. 超过 256 个异常时 UI 明确聚合展示，而不是静默截断。
12. Bad Case 能完整复现 View、Pair、Wipe、Diff、Threshold 和 ROI 状态。
13. Shortcut preset、OSC 和 DF/NDF 偏好重启后保持。
14. Main.qml 不再保存 Active/Staged Source 双重真值。
15. README、Release Notes 和 workflow 对签名、保存能力、媒体能力的描述完全一致。

---

# 最终判断

VCStation 1.2.0 的主体架构已经成功：它不再是 Project 工具，而是一个清晰的 1～3 路视频视觉审查工作站。

当前最高价值的工作不是继续增加新的比较模式，而是依次解决：

> **发布身份可信度 → 请求和 Reference 状态一致性 → 去 Project 后的架构残留 → Alignment 长视频扩展性 → 媒体兼容性。**

完成 1.2.1 的发布与正确性加固后，当前版本才适合被定义为稳定的 1.2 系列基线。
