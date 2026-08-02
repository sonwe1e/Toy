# 核心结论

`dev` 分支已经将项目版本推进到 **VCStation 1.4.0**，并完成了相较 1.2.0 很有价值的收口：删除 Workspace 薄壳、统一用户意图队列、拆分 Main.qml、持久化快捷键和 DF/NDF、支持裸路径启动、补充 1/2/3 文件 Explorer 命令、修复旧 `.dvsproj` 注册，以及增加两路 1080p60 门禁。

但按正式稳定版标准，1.4.0 目前仍应定义为：

> **功能完整度较高的 Release Candidate，而不是已经完成加固的稳定 Release。**

当前最关键的问题不是再增加新模式，而是修复六个基础缺陷：

1. `dev` 推送不会自动触发 Build/Quality，当前推送本身不能构成验证证据；
2. 发布包和 Explorer Shell DLL 全部无签名；
3. StartupRequest 与 ReviewIntent 两套队列可能发生停滞；
4. ReviewIntent 缺少 generation 和 source identity，排队操作可能作用到错误会话；
5. 打开/关闭失败时，部分 UI 状态会被提前清除；
6. 新的 `mapSurfacePoint()` 统一了接口，但 Wipe、Letterbox 和 Analysis Grid 的坐标语义仍不正确。

综合评估：

| 维度         | 当前判断           |
| ---------- | -------------- |
| 基础播放与逐帧    | 较成熟            |
| 1～3 路会话    | 主体完成，排队一致性仍有风险 |
| UI 信息架构    | 明显改善           |
| Alignment  | 短视频可用，长视频能力不足  |
| 媒体兼容性      | 仍是静音 SDR 视觉审查器 |
| 发布安全       | 不达稳定版标准        |
| CI 证据      | dev 推送不会自动完整验证 |
| 1.4.0 稳定发布 | 暂不建议           |

---

# 一、1.4.0 相较 1.2.0 的实际进步

## 1. Workspace 薄壳已经删除

1.4.0 不再经过：

```text
PlaybackCoordinator
→ WorkspaceCoordinator
→ WorkspaceController
→ QML
```

运行时直接组合 `PlaybackCoordinator`、`ReviewController` 和 Settings，关闭视频也由 `ReviewController::closeSources()` 完成。

这一修改是正确的，减少了已经失去 Project 职责的中间层。

## 2. Main.qml 已经开始按职责拆分

当前已经提取：

```text
ApplicationMenuBar.qml
ReviewShortcuts.qml
ReviewInputDialogs.qml
ComparisonViewport.qml
TabbedInspector.qml
```

菜单、快捷键、输入对话框和 Viewer 不再全部直接定义在 Main 中。

## 3. Settings 持久化明显改善

1.4.0 已将以下配置进入 Settings：

* Shortcut preset；
* DF/NDF Timecode；
* View mode；
* Difference metric/gain/pair/filter；
* OSC mode。

同时删除旧 `large-step-frames` 设置，并把 Settings completion 从 16 ms 轮询改为事件唤醒。

## 4. Explorer 和命令行入口更完整

现在 GUI 可以直接接受：

```text
VCStation.exe video.mp4
VCStation.exe a.mp4 b.mp4
VCStation.exe a.mp4 b.mp4 c.mp4
```

Explorer Shell Extension 也会根据选择数量显示不同标题，并支持 1～3 个本地视频。

## 5. 发布契约和安装升级加固有所进步

1.4.0 增加了生成式 Release Metadata、发布契约测试、旧 `.dvsproj` 注册清理和两路 1080p60 性能门禁。

这些修改表明 1.4.0 的主要问题已经从“大架构缺失”转向“边界一致性和发布质量”。

---

# 二、P0：发布和验证缺陷

## 1. 推送到 dev 不会自动执行主要 CI

当前 Build and Test 与 Quality workflow 只响应：

```yaml
pull_request:
push:
  branches:
    - main
```

因此，单纯推送到 `dev` 不会自动运行 Debug、Release、lint 和静态分析。硬件与性能 workflow 则只支持 `workflow_call` 或手动 `workflow_dispatch`。

这意味着：

> 当前推送只能证明代码存在，不能证明该 dev HEAD 已经通过 1.4.0 发布门禁。

### 技术路线

有两种合理方案。

**方案 A：PR 驱动，推荐**

```text
dev
→ 建立 dev → main PR
→ Build Debug/Release
→ Quality
→ 人工触发 Hardware/Performance，指定精确 SHA
→ 合并 main
→ 创建 v1.4.0 tag
```

**方案 B：dev 也运行非硬件 CI**

```yaml
push:
  branches:
    - main
    - dev
```

硬件测试仍保持手动，以免每次 dev 推送占用 GPU 工作站。

必须把精确 commit SHA 写入所有测试产物，不能只记录 branch 名称。

---

## 2. 无签名 MSI 和 Explorer DLL 不适合作为稳定版

Release workflow 明确只允许发布无签名的 `v1.4.0`，最终产物只有 ZIP、MSI 和 SHA256SUMS，没有 Authenticode 签名。Release Notes 也明确说明 EXE、DLL 和 MSI 都未签名。

其中风险最高的是：

```text
VCStationShell-1.4.dll
```

它会被 Windows Explorer 进程加载。未签名 COM DLL 比普通便携 EXE 更容易受到 SmartScreen、企业策略和安全软件拦截。

### 技术路线

稳定版必须恢复：

```text
签名 VCStation.exe
→ 签名 VCStationCli.exe
→ 签名 VCStationShell-1.4.dll
→ 打包 ZIP/MSI
→ 签名 MSI
→ signtool verify /pa
→ 对签名后的产物执行 packaged smoke
```

签名暂时无法解决时，发布应拆成：

```text
Unsigned Preview ZIP
    不注册 Explorer Shell Extension

Signed Stable MSI
    包含 Explorer Shell Extension
```

SHA-256 只能证明文件未被修改，不能证明发布者身份。

---

## 3. 升级门禁没有覆盖最实际的 1.2.0→1.4.0

当前 Release workflow 下载的是 `v1.1.0` MSI，Packaged Smoke 也将前置版本硬编码为 1.1.0。

1.1.0→1.4.0 对验证旧 `.dvsproj` 清理有价值，但真实用户最可能执行的是：

```text
1.2.0 → 1.4.0
```

1.2.0 包含新的 Settings、Shell DLL 和 UI 状态，不能由 1.1.0 升级测试完全替代。

### 技术路线

升级矩阵应改成：

```text
1.2.0 → 1.4.0
    主升级路径
    验证 Settings、Shell DLL、快捷方式和功能

1.1.0 → 1.4.0
    遗留迁移路径
    重点验证 .dvsproj 和旧注册表清理
```

`VerifyMsiPackage.ps1` 不应硬编码 1.1.0，而应接收：

```powershell
-PreviousMsiPath
-PreviousExpectedVersion
-ExpectLegacyProjectAssociation
```

---

## 4. per-machine MSI 仍使用 HKCU 作为组件 KeyPath

开始菜单快捷方式安装在 Common Programs，Package 是 per-machine，但组件 KeyPath 使用：

```text
HKCU\Software\VCStation\Installer
```

这在多用户 repair、自愈和卸载场景中容易产生组件状态不一致。

### 技术路线

改为：

```xml
<RegistryValue
    Root="HKLM"
    Key="Software\VCStation\Installer"
    Name="StartMenuShortcut"
    ...
    KeyPath="yes"/>
```

并增加：

```text
管理员安装
→ 普通用户登录
→ 快捷方式存在
→ MSI repair
→ 卸载
→ 所有用户公共快捷方式和 HKLM KeyPath 被清理
```

旧 `SupportedTypes` 最好删除具体 `.dvsproj` Value，而不是无条件删除整个 SupportedTypes Key，避免未来误删其他关联。

---

# 三、P0：ReviewIntent 与 StartupRequest 状态机存在缺陷

## 1. 目前仍然有两套队列

1.4.0 同时存在：

```text
startupRequests_
reviewIntents_
```

前者管理单实例和 Explorer 请求，后者管理打开、替换、Reference 和关闭。

Main 中又通过：

```qml
drainStartupRequestQueue()
finishActiveStartupRequest()
```

把 StartupRequest 转换为 UI 操作。

## 2. 多个外部请求可能永久停在 Startup 队列

当前流程是：

```text
取出第一个 StartupRequest
→ 设置 startupRequestActive=true
→ 提交打开视频
→ 立即 completeStartupRequest()
→ 若还有请求，发 startupRequestAvailable
```

但 `drainStartupRequestQueue()` 在 `busy == true` 时直接返回。后续 busy 结束时，没有对应的 `onBusyChanged` 再次唤醒 Startup 队列。

因此以下场景可能停滞：

```text
Explorer 请求 A/B
→ 开始打开，controller busy

紧接着 Explorer 请求 C/D
→ 请求进入 Startup 队列

第一个请求 complete 时发通知
→ 此时仍 busy
→ drain 直接返回

第一个打开完成
→ 没有新的 StartupRequestAvailable
→ C/D 一直停留
```

### 临时修复

至少增加：

```qml
onBusyChanged: {
    if (!busy)
        Qt.callLater(drainStartupRequestQueue)
}
```

### 正确技术路线

彻底取消两套队列，统一成：

```cpp
struct ReviewIntent {
    IntentId id;
    ReviewIntentKind kind;
    ReviewIntentOrigin origin;
    QList<QUrl> sources;
    SourceIdentity referenceSource;
};
```

所有入口都进入同一个队列：

```text
File Menu
Drag and Drop
Explorer
Command Line
Add Video
Remove Video
Change Reference
Close Videos
```

外部请求不再通过 QML 进行二次排队。

---

## 3. Intent 队列缺少容量限制

Startup 队列限制为九个活动加排队请求，但 `reviewIntents_` 没有上限。连续点击 Reference、Remove 或 Close 可以不断增加队列。

### 技术路线

设置：

```cpp
static constexpr std::size_t kMaximumQueuedIntents = 8;
```

并定义明确的合并策略：

| Intent           | 合并策略                   |
| ---------------- | ---------------------- |
| Open new videos  | 新请求替换所有旧 Open/Replace  |
| Add/Remove       | 按 Source Identity 进行重建 |
| Change Reference | 只保留最后一次                |
| Close            | 清空其他排队操作并放在队首          |
| Exit             | 清空全部并进入 shutdown       |

超出上限时返回可翻译的结构化错误，而不是继续增长。

---

## 4. 排队 Intent 没有 generation 前置条件

当前 `ReviewIntent` 只保存：

```cpp
kind
sources
referenceIndex
```

没有记录它基于哪个活动会话生成。

例如：

```text
当前活动 A/B
→ 正在打开 C/D
→ Active Sources 为保证画面稳定，仍显示 A/B

此时用户点击 Add E
→ 排队内容根据 A/B+E 构造

C/D 成功
→ 队列继续执行 A/B/E
→ 刚打开的 C/D 又被旧拓扑覆盖
```

Reference 也存在同样问题：排队的 `referenceIndex=1` 在新会话里可能已经代表另一个文件。

现有测试只验证简单的 FIFO 顺序，没有验证 topology 变化后的 stale intent。

### 技术路线

```cpp
struct ReviewIntent {
    IntentId id;
    ReviewIntentKind kind;

    SessionGeneration expectedGeneration;
    QList<SourceIdentity> expectedSources;

    QList<QUrl> desiredSources;
    std::optional<SourceIdentity> desiredReference;
};
```

执行前：

```text
generation 相同
    → 直接执行

generation 不同，但操作可按 identity 重放
    → 重建 Add/Remove

generation 不同且无法安全重放
    → 拒绝并提示“视频列表已变化，请重新操作”
```

Reference 必须使用 Source Identity，而不是数组索引。

---

## 5. UI 只显示五秒 Toast，不能管理排队请求

`queuedIntentCount` 已经暴露，但主界面只显示一个五秒 Toast；用户看不到：

* 当前排队数量；
* 哪个操作正在执行；
* 哪个操作替换了前一个；
* 如何取消。

此外，C++ 中的 `Open videos queued` 等文案使用 `QStringLiteral`，无法通过 `qsTr` 本地化。

### 技术路线

C++ 只发结构化状态：

```cpp
struct IntentNotification {
    IntentId id;
    IntentStatus status;
    IntentKind kind;
    ErrorCode error;
};
```

QML 负责翻译和表现：

```text
Opening 3 videos…
1 request queued
Reference change failed
[Cancel queued request]
```

排队期间，对应 Source Chip 或 Reference 项目应显示小型 Pending Indicator。

---

# 四、P0：会话状态的提交时机不正确

## 1. 新视频尚未打开成功，Range 就已经被清除

单视频新开时，Main 会先调用：

```qml
clearSelectedRange()
sourceOffsetValues = {}
```

之后才提交打开请求。多视频确认后也会在提交前清理 Range。

若新文件打开失败，后端可能恢复旧会话，但旧会话的 In/Out 已经丢失。

## 2. Close 只要“进入队列”就立即清空 UI

当前：

```qml
if (shell.closeSources())
    clearReviewUi()
```

而 `closeSources()` 返回 true 可能只表示“请求已排队”，并不表示关闭已经成功。

如果 Close 最终失败，画面仍在，但 Range、Wipe、ROI 和其他 UI 状态已经被清空。

## 3. 新会话成功后，部分旧视觉状态反而没有重置

新开视频时只清 Range 和 Offset，但以下状态可能继续保留：

* Wipe position；
* Threshold；
* ROI；
* Zoom/Pan；
* 当前 Diff 参数。

这会让新视频以旧会话的局部放大或 ROI 打开。

### 技术路线

所有状态变更必须以 Command Terminal 为事务边界：

```cpp
signal intentFinished(
    IntentId id,
    IntentKind kind,
    IntentOutcome outcome,
    ErrorCode error
);
```

策略：

```text
NewReview 成功
    清 Range
    Wipe=50%
    Threshold off
    Clear ROI
    Reset zoom/pan
    清 manual offset staging

ReplaceSources 成功
    按 MediaTime 保留 Range
    保留 zoom/ROI 或按明确策略重映射

ChangeReference 成功
    按 MediaTime 保留 Range
    保留 View

Close 成功
    清空所有运行态

任何失败
    保持旧 UI 与旧会话完全不变
```

QML 不应在 `submit()` 返回 true 时提交视觉状态。

---

# 五、P0：Surface 命中映射仍然不正确

1.4.0 新增了 `ComparisonSurface::mapSurfacePoint()`，这是正确方向，但当前实现只是根据 `sourcePanelRects` 将坐标归一化。

ComparisonViewport 将它直接用于：

* Zoom；
* Pan；
* ROI。

## 1. Wipe 的归一化坐标错误

假设 Wipe 位置为 25%，用户点击画布宽度的 20%。

当前左侧 Panel 宽度是 25%，所以得到：

```text
normalizedX = 20% / 25% = 0.8
```

但 Wipe 两侧显示的是两张完整图像的裁切，正确的图像坐标应该约为：

```text
normalizedX = 0.2
```

因此 Wipe 状态下的鼠标缩放焦点和 ROI 会发生明显偏移。

## 2. Letterbox 区域被当成视频内容

`mapSurfacePoint()` 只知道 Panel Rect，不知道视频经过 aspect-fit 后的真实 Content Rect。用户在黑边区域滚轮或 Shift 拖动时，也会得到一个合法的归一化坐标。

## 3. Analysis Grid 的 Difference Cell 没有命中区域

方法只遍历 `sourceRects`，没有处理 `differenceRect`。在 Difference Cell 上缩放或 ROI 时可能没有反应。

### 技术路线

不能用 Panel Layout 直接代替 Interaction Geometry。

新增：

```cpp
enum class SurfaceRegionKind {
    Source,
    Difference,
    WipeComposite,
    Empty
};

struct SurfaceHitResult {
    SurfaceRegionKind region;
    int panelIndex;
    int sourceSlot;

    bool insidePanel;
    bool insideContent;

    double contentX;
    double contentY;
};
```

Renderer 和 UI 共同使用：

```cpp
SurfacePresentationGeometry computePresentationGeometry(
    layout,
    sourceDisplaySizes,
    rotations,
    sampleAspectRatios,
    viewportTransform,
    roi
);
```

Wipe 必须使用完整共同 Content Rect 计算坐标，只用分割位置判断当前位于哪一侧。

必须覆盖：

```text
Single / Side / Wipe / Diff / Three-up / Reference Focus / Analysis Grid
16:9 / 9:16 / 不同分辨率
Letterbox / Rotation / SAR
100% / 125% / 150% DPI
Wipe 5% / 25% / 50% / 95%
```

---

# 六、P1：键盘输入仍可能与媒体快捷键冲突

媒体快捷键全部使用 `Qt.ApplicationShortcut`，包括：

* A/D；
* I/O；
* Space；
* Ctrl+A/Ctrl+D；
* Tab。

Main 通过查找 `blocksGlobalMediaShortcuts` 来判断是否禁用快捷键，但部分可编辑控件没有该属性：

* `ReviewOffsetSpinBox`；
* Threshold SpinBox；
* Manual Anchor 中的 SpinBox 和 ComboBox。

可能出现：

```text
正在修改 Offset
→ 按 A/D
→ 视频逐帧

正在输入 Threshold
→ 按 Space
→ 播放启动

Dialog 打开
→ 按 Tab
→ 整个界面 Chrome 被隐藏
```

### 技术路线

建立统一 `InputScope`：

```cpp
enum class InputContext {
    Viewer,
    TextEditing,
    Popup,
    ModalDialog
};
```

媒体快捷键启用条件：

```text
Viewer
    全部启用

TextEditing
    只允许 Esc

Popup
    让 Popup 消费方向键、Enter、Esc

ModalDialog
    禁止 Tab/F11/播放和逐帧
```

短期至少将所有自定义输入组件加上：

```qml
property bool blocksGlobalMediaShortcuts: true
```

但长期应由 Window 级 Input Controller 根据 activeFocusItem 类型和 Overlay 状态统一判断。

---

# 七、P1：UI 与架构仍有未完成的拆分

## 1. ReviewSessionFacade 目前只是类型别名

当前实现为：

```cpp
using ReviewSessionFacade = ReviewShellController;
```

它并没有建立新的接口边界。`ReviewShellController` 同时管理：

* Active/Staged Sources；
* Intent 队列；
* Startup 队列；
* Chrome；
* Inspector；
* In/Out；
* Loop 状态；
* Pending Action。

### 技术路线

真正实现：

```text
ReviewSessionFacade
├── SourceSessionModel
├── ReviewIntentQueue
├── RangeSelectionModel
├── StartupIngress
└── PresentationShellState
```

或者分成：

```text
ReviewSessionController
PresentationController
StartupRequestController
```

对 QML 暴露的是稳定 Facade，而不是旧 Controller 的别名。

## 2. Main.qml 仍承担大量业务逻辑

Main 仍维护：

* Difference Threshold；
* Wipe；
* Source Offset staging；
* Range loop 驱动；
* StartupRequest 转换；
* 打开与关闭事务；
* Error mapping；
* HUD；
* Toast。

建议最终目标：

```text
Main.qml：不超过 300～500 行
只负责布局和组件连接
```

业务状态进入强类型 C++ ViewModel。

## 3. Source Chip 单视频文本可能过早截断

单视频时 `roleButton.visible=false`，但文件名 Text 的右锚点仍然连接到 `roleButton.left`。同时 Chip 宽度只按文件名加 24 px 计算。

隐藏的 Role Button 仍保留几何，因此单视频文件名会比预期更早 Elide。

应改为：

```qml
right: control.singleMode ? parent.right : roleButton.left
rightMargin: control.singleMode ? 12 : 6
```

## 4. 两路视频仍可通过顶部菜单选择 Three-up 和 Reference Focus

顶部 Compare → Layout 中：

* Three up 没有限制 `sourceCount === 3`；
* Reference focus 也没有限制；
* 只有 Analysis Grid 有限制。

两路时点击这些模式，`preferences.viewMode` 会改变，但 Renderer 通过 `effectiveViewMode` 回退成 Side-by-side。菜单可能显示 Three-up 已选中，而实际画面仍是 Side-by-side。

### 修复

```qml
enabled: control.sourceCount === 3
visible: control.sourceCount === 3
```

菜单的 checked 状态必须基于 `effectiveViewMode`，不能基于原始 Preference。

---

# 八、P1：运行时和性能门禁问题

## 1. GraphicsNotificationPump 每 2 ms 唤醒一次

Runtime 的 Graphics Pump 使用：

```cpp
wait_for(..., 2ms)
```

但生产者并没有在新 notification 到达时唤醒该 condition variable，因此即使空闲，也会每秒唤醒约 500 次。

这与 1.4.0 新增的“低 CPU 影响”CI 目标并不一致。

### 技术路线

由 `GraphicsDeviceBroker` 提供真正的阻塞等待：

```cpp
std::optional<GraphicsDeviceNotification>
waitForNotification(std::stop_token stopToken);
```

或使用 Win32 Event：

```text
Publish notification
→ SetEvent

Consumer
→ WaitForMultipleObjects(notificationEvent, shutdownEvent)
```

删除 2 ms polling。

---

## 2. Release 性能门禁在 BelowNormal 和固定四核下运行

`invoke-low-impact.ps1` 将当前进程：

* 限制到最后四个逻辑 CPU；
* 设置为 BelowNormal；
* CMake 并发限制为 4；
* CTest 并发限制为 1。

`run-performance-gate.ps1` 又把相同 Affinity/Priority 传给 VCStation 性能进程。

这对共享构建机是友好的，但作为性能基准存在问题：

* 最后四个逻辑 CPU 在混合架构 CPU 上可能全部是 E-Core；
* Windows Processor Group 超过 64 逻辑 CPU 时 Affinity 语义不稳定；
* BelowNormal 容易被桌面负载抢占；
* 测量结果可能更多反映 Runner 当时的系统噪声，而不是软件性能。

### 技术路线

拆成两个资源档位：

```text
quality-low-impact
    4 CPU
    BelowNormal
    用于 Build / Unit / Lint

performance-isolated
    独占 GPU Runner
    Normal priority
    固定高性能电源计划
    禁止并行其他任务
    记录 CPU/GPU/驱动/刷新率
```

可以额外保留：

```text
degraded-4cpu-resilience
```

作为受限资源稳定性测试，但不能代替正式性能基准。

---

## 3. 当前性能矩阵只证明 Source 数量，不完全证明交互路径

当前已经补齐：

```text
1/2/3 × 1080p60
1/2/3 × 1080p120
```

这是明显改善。

但还应增加场景型门禁：

```text
2×1080p60 Side-by-side 5 min
2×1080p60 Wipe 5 min
2×1080p60 Diff 5 min
Reference A→B 重建
1→2→3→2 Source topology rebuild
连续 seek/step
Device Lost 恢复
```

单纯打开 N 路播放不能证明每种 Renderer 路径都满足帧预算。

---

# 九、Alignment 仍是 1.4.0 的主要能力瓶颈

1.4.0 Release Notes 明确说明没有进一步扩展 Alignment。

## 1. Global Offset 仍然只搜索 ±16 帧

当前默认：

```cpp
minimumOffset = -16;
maximumOffset = 16;
```

手动 Offset SpinBox 也是 `[-16, 16]`。

对应：

```text
60 FPS：约 ±267 ms
120 FPS：约 ±133 ms
```

两个录制起点只要相差一秒，自动估计就无法覆盖。

### 技术路线：多尺度 Offset

```text
第一层：时间级粗搜索
    ±10～60 秒
    每 100～500 ms 一次低分辨率特征

第二层：帧级细搜索
    在粗结果附近搜索 ±1～2 秒

第三层：局部序列校正
    检测缺帧、重复帧和漂移
```

参数应以时间而不是帧表示：

```cpp
std::chrono::milliseconds maximumOffset;
```

---

## 2. Sequence Alignment 仍限制为 50,000 帧

`SequenceAlignmentRequest.maximumFrameCount` 仍为 50,000。

这大约等于：

```text
30 FPS：27.8 分钟
60 FPS：13.9 分钟
120 FPS：6.9 分钟
```

对于长游戏视频明显不足。

### 技术路线：分块 Alignment

```text
粗场景切分
→ 2～5 分钟 Chunk
→ Chunk 间保留重叠区
→ 每块执行 Banded Alignment
→ 通过单调锚点拼接
→ 冲突区标记 Review Required
```

保持计算复杂度：

```text
O(chunk_size × band_width)
```

避免全视频 N×M 矩阵。

---

## 3. Timeline 最多只投影 256 个 Marker

ReviewController 仍有：

```cpp
kMaximumAlignmentTimelineMarkers = 256
```

异常超过 256 个时，用户不会获得完整信息。

### 技术路线

采用两级投影：

```text
全局：
    按时间 Bucket 聚合异常密度

局部：
    根据 Timeline Zoom 查询详细 Marker
```

接口：

```cpp
markerBuckets(startTime, endTime, bucketCount)
markerDetails(startTime, endTime, limit)
```

界面应明确显示：

```text
1,284 anomalies · aggregated view
```

不能静默截断。

---

## 4. 缺少独立的 Alignment 特征缓存

删除 Project 时，原先与 Project 绑定的缓存也被删除。反复打开相同素材并执行序列分析，需要重新解码签名。

缓存不应重新引入 Project，而应独立存在：

```text
Cache key =
    Source quick fingerprint
    + canonical source fingerprint
    + algorithm version
    + options
```

缓存内容：

* 低分辨率签名；
* 粗 Offset；
* Sequence segment；
* 异常摘要。

缓存仅作为性能优化，用户关闭视频后仍不保存会话。

---

# 十、产品能力不足，但不属于代码错误

## 1. Bad Case 导出被完全删除

1.4.0 明确声明不保存 Bad Case，File 菜单也只剩 Open、Add、Close 和 Exit，ReviewController 已没有 Export API。

这与删除 Project 是两件事：

```text
Project
    保存整个会话

Evidence Export
    导出当前发现的问题
```

对于专业评测工具，删除 Evidence Export 会降低结果闭环能力。

### 技术路线

重新增加独立 `EvidenceExportService`，但不恢复 Project：

```text
comparison.png
evidence.json
```

JSON 应包含：

* App version；
* Canonical MediaTime/Frame；
* Source 文件名和快速指纹；
* Reference；
* Alignment 状态；
* View mode；
* Pair；
* Wipe position；
* Diff metric/gain/threshold；
* ROI/Zoom。

导出必须异步执行。

---

## 2. 仍然没有音频

1.4.0 已明确将产品定义为静音视觉审查器，空状态也显示 “Audio is not played”。

这在产品定义上已经诚实，但限制了它作为普通单视频播放器的价值。

完整路线：

```text
FFmpeg audio stream selection
→ Audio decoder
→ libswresample
→ WASAPI shared mode
→ Audio ring buffer
→ A/V clock
→ Seek flush
→ Pause/resume
```

多路比较默认只播放 canonical source 的音频。

---

## 3. 没有 AV1、VP9 和 HDR

1.4.0 明确没有新增 VP9、AV1 和 HDR；依赖中也没有音频重采样组件。

建议后续顺序：

```text
1. VP9 / WebM
2. AV1 软件解码
3. AV1 硬件能力探测与 fallback
4. PQ / HLG
5. 线性光和显示参考 Diff
6. Tone Mapping
```

Tone-mapped 差异不能标记为 pixel-exact。

---

# 十一、Explorer Shell 的剩余问题

Shell DLL 会在 `GetTitle`、`GetToolTip`、`GetState` 和 `Invoke` 中重复读取选择项、调用 `GetFileAttributesW` 并检查网络路径。它还明确拒绝 UNC/网络路径。

这有两个问题：

1. Explorer UI 线程上进行磁盘状态查询，离线盘或慢盘可能拖慢右键菜单；
2. 视频工作流常使用 NAS/SMB，网络路径完全不可用。

### 技术路线

Shell DLL 只做轻量工作：

```text
GetState：
    检查数量和扩展名
    不访问磁盘

Invoke：
    构造 Unicode 命令行
    启动 VCStation
```

文件是否存在、是否可读、是否为网络路径，由 VCStation 异步验证。

应允许 UNC：

```text
\\server\share\video.mp4
```

同时在应用层给出明确加载状态和失败提示。

---

# 十二、推荐执行路线

## 1.4.1：正确性与发布加固

必须完成：

1. 建立 dev→main PR，跑 Debug/Release/Quality；
2. 手动对精确 SHA 跑 Hardware/Performance；
3. 恢复签名，或将无签名版本降级为 Preview ZIP；
4. 增加 1.2.0→1.4.0 升级门禁；
5. 修复 per-machine MSI 的 HKCU KeyPath；
6. 合并 StartupRequest 与 ReviewIntent 队列；
7. 为 Intent 增加 generation、source identity 和容量限制；
8. 只在 terminal success 后重置 UI；
9. 修正 Wipe/Letterbox/Difference hit test；
10. 修复媒体快捷键与输入控件冲突；
11. 禁止两路模式选择 Three-up/Reference Focus；
12. 修复单视频 Source Chip 的文件名布局。

## 1.5.0：专业审查能力

1. 恢复独立 Evidence Export；
2. 实现多尺度 Offset 搜索；
3. 实现分块 Sequence Alignment；
4. 增加 Alignment Signature Cache；
5. Timeline Marker 改为聚合查询；
6. 将 ReviewSessionFacade 改为真实类型；
7. 将 Main.qml 继续缩减；
8. Explorer 支持 UNC。

## 1.6.0：媒体播放器能力

1. Canonical Audio；
2. VP9/WebM；
3. AV1；
4. HDR/PQ/HLG；
5. Display-referred 与 source-linear Diff；
6. 可选字幕和音轨选择。

---

# 十三、1.4.0 最低验收标准

1. dev 精确 SHA 的 Debug、Release、Quality 全绿。
2. Hardware/Performance 对同一 SHA 全绿。
3. 稳定 MSI、EXE、CLI 和 Shell DLL 均有有效签名。
4. 1.2.0→1.4.0 和 1.1.0→1.4.0 均通过。
5. 连续两个以上 Explorer 请求不会停在 Startup 队列。
6. 排队 Add/Remove/Reference 不会作用到错误 generation。
7. 打开或关闭失败后旧 UI 状态完全保持。
8. Wipe 25% 位置点击 20% 处，映射结果仍为约 20%，而不是 80%。
9. Letterbox 区域不能创建 ROI。
10. Analysis Grid 的 Difference Cell 可以正确 Zoom/Pan。
11. 输入 Threshold、Offset、Anchor 时，媒体快捷键不会触发。
12. 两路菜单不能显示 Three-up 已选中而画面仍是 Side-by-side。
13. Intent 队列有上限、可查看、可取消。
14. 2 ms Graphics polling 被移除。
15. 正式性能门禁在独占、可记录的硬件环境运行。
16. 超过 256 个异常时显示聚合数量，不静默截断。
17. 长于 50,000 帧的素材有明确的分块分析方案。
18. 用户能明确知道当前版本没有音频、AV1、HDR 和 Evidence Export。

---

# 最终判断

1.4.0 已经完成了一次高价值的架构清理，尤其是 Workspace 删除、QML 拆分、Settings 事件化、裸路径启动、Explorer 1～3 路和两路 1080p60 门禁。

但当前最大的风险是：

> **表面上已经统一成 Session Facade，实际上仍是两套队列、索引式排队操作和提交前 UI 状态修改；发布层则仍是无签名且未自动验证 dev HEAD。**

优先解决发布身份、Intent 事务和 Surface 坐标三个核心问题后，1.4 系列才能成为可靠的稳定基线。
