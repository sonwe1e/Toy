# 核心结论

这批新需求是合理的，而且会把 VCStation 从“专用多视频比较器”进一步完善为：

> **支持 1～3 路视频、以逐帧审查为核心，同时兼具单视频播放、资源管理器快捷启动和高可用对比交互的 Windows 视频工作站。**

下一步不建议把所有修改都塞进一个补丁版本。应拆成两个阶段：

| 版本         | 定位       | 内容                                                   |
| ---------- | -------- | ---------------------------------------------------- |
| **v1.1.1** | 交互与可靠性修复 | 彻底删除 4K 测试、修复透明确认框、修正 Wipe 坐标、扩大拖动块、重做底部控制按钮、恢复签名    |
| **v1.2.0** | 能力扩展     | 支持单视频模式、1～3 路统一会话模型、Windows 右键对比、启动参数协议、项目 Schema v4 |

其中需要特别说明：当前仓库并没有真正删除 4K 测试，只是从 v1.1.0 的发布门禁中排除了它。`performance.4k30-main10` 仍在 CMake 中注册，脚本也仍接受该 Profile，工作流将它描述为“deferred”。下一步应按你的决定从活动测试、脚本、素材清单和文档中彻底移除。

---

# 一、对六项新需求的具体评估

## 1. 后续彻底取消 4K 测试

这个决策符合你的真实工作负载。既然绝大多数素材在 1080p、60 FPS，少数为 120 FPS，那么性能预算、缓存策略和发布门禁都应该围绕这些场景设计，而不是继续为几乎不用的 4K 输入增加测试时间和维护成本。

需要删除的内容包括：

```text
tests/hardware/CMakeLists.txt
    performance.4k30-main10

tools/run-performance-gate.ps1
    ValidateSet 中的 4k30-main10
    4K fixture 映射

.github/workflows/hardware-performance.yml
    4K deferred 注释
    exclude-regex
    release-gate-policy 中的 4K 描述

docs/self-hosted-runner.md
    4K 素材要求

外部性能素材目录
    gate-4k30-main10-*.mp4
```

但应继续保留小分辨率的 P010、10-bit、D3D11VA 和零拷贝正确性测试。这些测试验证的是像素格式和硬件路径，不属于 4K 性能测试。

新的硬件矩阵建议改成：

```text
1 × 1080p60
1 × 1080p120

2 × 1080p60
2 × 1080p120

3 × 1080p60
3 × 1080p120
```

其中单视频测试将在 v1.2.0 加入。连续播放门禁可以控制在 60～120 秒，更符合实际视频长度；内存泄漏和线程泄漏则通过多轮打开、播放、跳转、关闭的 lifecycle soak 检测，而不是依赖长时间 4K 播放。

---

## 2. 两视频拖入后的确认框透明

这不是单纯的配色问题。当前 `Main.qml` 同时无别名导入：

```qml
import QtQuick.Controls
import QtQuick.Dialogs
```

随后直接使用 `Dialog`。Qt Quick Controls 与 Qt Quick Dialogs 都存在 Dialog 类型，这会造成类型来源、Popup 类型和平台呈现方式不够明确。当前确认框虽然自定义了 `background`，但没有显式指定 `parent: Overlay.overlay`、`popupType`、`dim`、Overlay 遮罩和完整 footer 样式。

Qt 官方推荐将场景内 Popup 显式放到 `Overlay.overlay`，并可以通过 `Popup.Item` 保证它在同一 Qt Quick scene 中渲染；`Overlay.modal` 用于定义模态背景遮罩。([Qt 文档][1])

### 修复方案

首先消除类型名冲突：

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as NativeDialogs
```

随后：

```qml
NativeDialogs.FileDialog { ... }
NativeDialogs.FolderDialog { ... }
```

确认界面应单独提取为 `DropConfirmationDialog.qml`，该文件只导入 `QtQuick.Controls`：

```qml
Dialog {
    id: control

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    popupType: Popup.Item

    modal: true
    dim: true
    focus: true
    padding: 20

    width: Math.min(600, parent.width - 48)
    implicitHeight: contentColumn.implicitHeight
                    + header.implicitHeight
                    + footer.implicitHeight
                    + topPadding
                    + bottomPadding

    Overlay.modal: Rectangle {
        color: "#99060a10"
    }

    background: Rectangle {
        color: "#ff171e2a"
        radius: 10
        border.width: 1
        border.color: "#40516a"
    }

    footer: DialogButtonBox {
        background: Rectangle {
            color: "#ff111823"
        }
    }
}
```

关键点是：

* 背景颜色显式使用不透明 Alpha；
* 强制 `Popup.Item`；
* 显式挂在窗口 Overlay；
* footer 不依赖平台主题默认透明度；
* 不再让确认框与 Native FileDialog 共用一个未限定的 `Dialog` 名称。

### 测试方案

当前 QML contract test只验证根组件能实例化，并没有真正打开确认框或检查像素，因此透明问题没有被捕获。

需要增加 WARP 视觉测试：

1. 打开确认框。
2. 使用 `grabWindow()` 捕获窗口。
3. 确认对话框中心像素 Alpha 为 255。
4. 确认框外区域被半透明遮罩变暗。
5. 检查 OK/Cancel footer 背景不透明。
6. 在 100%、125%、150% DPI 下执行。
7. 执行确认和取消，验证 source 顺序没有意外改变。

---

## 3. 支持单视频播放

这项需求不能只在 QML 中把 Open 按钮放开，因为当前“至少两路”是贯穿整个系统的结构性不变量：

* `ComparisonValidator` 要求 2～3 路；
* `FrameSet::create()` 拒绝少于两路；
* `SessionSnapshot::isConsistent()` 要求 Ready 状态至少两路；
* `MultiSourceFrameProvider` 要求 2～3 路；
* QML Open 按钮要求 A、B 都存在；
* `Project::create()` 会直接访问 `sources[0]` 和 `sources[1]` 创建 difference edge。

正确路线不是新增一套独立的 `SingleVideoPlayer`，也不应引入 Qt Multimedia 形成第二套时钟。应把现有模型从：

```text
2～3 路 Comparison Session
```

泛化为：

```text
1～3 路 Review Session
```

这样单视频仍然复用：

* FFmpeg probe；
* 精确帧索引；
* D3D11VA；
* SourceDecodeActor；
* FrameSet；
* GPU transfer；
* Presentation ACK；
* 播放时钟；
* 逐帧导航；
* 预取缓存。

### 单视频模式的产品定义

建议将其明确为：

> **单视频画面播放和逐帧审查模式，默认不增加音频链路。**

拖入单个视频后：

1. 立即打开视频；
2. 显示第一帧并保持暂停；
3. Space 开始播放；
4. 可以选择设置“单视频拖入后自动播放”；
5. 视频充满整个 viewport；
6. 隐藏 Reference、Compare Pair、Diff、Wipe、Alignment 等无意义功能。

若未来需要声音，那将涉及音频解码、输出设备、A/V 时钟、seek 后音频 flush 和同步策略，应作为独立需求处理，而不是混入这次单视频泛化。

### 具体底层修改

```text
ComparisonValidator.cpp
    kMinimumSources: 2 → 1

FrameSet.h
    sources.size() < 2 → sources.empty()

SessionSnapshot.cpp
    Ready: sources.size() >= 2 → >= 1

MultiSourceFrameProvider.cpp
    允许 1～3 路
    单视频只创建一个 SourceDecodeActor

ReviewController
    新增 openSources(QVariantList urls, int referenceIndex)
    不再依赖固定 A/B/C 参数

D3d11ComparisonRenderer
    新增 Single View
    将 Source 0 aspect-fit 到完整 viewport

AlignmentAnalysisService
    单视频模式禁用分析命令

QML
    拖入一个视频后直接 openSources()
    Open 按钮只要求 Source A
    单视频模式隐藏所有对比控件
```

`GpuFrameSet` 本身没有要求至少两路，因此 GPU transfer 层相对容易适配；真正需要调整的是 FrameSet、validation、snapshot 和 renderer。

### 枚举兼容性

新增单视频布局时不能把它插到枚举开头，否则旧设置文件中的数值会改变含义。应使用显式值：

```cpp
enum class ViewMode {
    SideBySide    = 0,
    ThreeUp       = 1,
    ReferenceFocus = 2,
    Difference    = 3,
    AnalysisGrid  = 4,
    Wipe          = 5,
    Single        = 6,
};
```

---

## 4. Wipe 拖动块太小，并且与视频分割位置不一致

当前不一致有一个明确的技术原因。

QML 中拖动线的位置是按整个 `dualVideoSurface.width` 计算的：

```qml
x = surfaceWidth * wipePosition
```

鼠标坐标又先映射到 `viewportFrame`，再除以整个 viewport 宽度。

但 D3D11 renderer 并不是在整个 viewport 上使用 `wipePosition`。它先对视频执行 aspect-fit，得到一个可能带黑边的 `destination`，然后把 `wipePosition` 当作 `destination.width` 的比例。

所以现在存在两个坐标域：

```text
QML 拖动块：整个 viewport 的 50%
Renderer 分割：视频内容矩形的 50%
```

只要视频存在左右 letterbox、窗口尺寸变化或显示比例不同，二者就不会重合。

### 统一坐标语义

建议规定：

> `wipePosition` 永远表示 ComparisonSurface 完整逻辑宽度中的归一化 X 坐标。

Renderer 中改为：

```cpp
const float splitX =
    bounds.x + bounds.width * clamp(wipePosition, 0.0F, 1.0F);

const float contentPosition =
    clamp((splitX - destination.x) / destination.width, 0.0F, 1.0F);
```

然后分别裁切：

```cpp
left.destination.right  = splitX;
right.destination.left  = splitX;

left.sourceUv.right =
    uv.left + uv.width * contentPosition;

right.sourceUv.left =
    uv.left + uv.width * contentPosition;
```

这样实际像素切换位置始终与界面拖动线一致，即使画面有 letterbox。

### QML 侧修改

鼠标坐标必须映射到 `dualVideoSurface`，而不是外层 `viewportFrame`：

```qml
function updateWipePosition(itemX) {
    const point = wipeHitArea.mapToItem(
        dualVideoSurface,
        itemX,
        0
    );

    root.wipePosition = Math.max(
        0,
        Math.min(1, point.x / dualVideoSurface.width)
    );
}
```

拖动块建议调整为：

```text
分割线宽度：3 px
可见手柄：44 × 60 px
鼠标命中区域：64 px 宽、覆盖整个分割线高度
```

使用 `DragHandler` 替代普通 `MouseArea`，保证拖动过程中持续持有 pointer grab；双击手柄恢复到 50%。

### 验收测试

在 25%、50%、75% 三个位置测试：

* QML 手柄中心；
* D3D 中实际左右图像切换像素；
* 二者误差不得超过 1 个物理像素。

还必须覆盖：

* 视频左右 letterbox；
* 16:9 对比 9:16；
* 100%、125%、150% DPI；
* ROI；
* zoom/pan；
* A/B、A/C、B/C 三组 edge。

---

## 5. 底部按钮语义不一致

当前底部依次使用：

```text
First
-1 s
-5
-1
Play
+1
+5
+1 s
Last
```

同时快捷键又分为 A/D、Shift+A/D、Ctrl+A/D 和 Up/Down，顶部还有一个 5/10 Frame Step 设置。这几套语义相互重叠，确实容易让用户困惑。

### 推荐布局

保留九个紧凑的图标按钮，但不再显示裸数字：

```text
⏮  回到首帧
↶  后退 1 秒
◀◀ 后退 5 帧
◀| 上一帧
▶  播放 / 暂停
|▶ 下一帧
▶▶ 前进 5 帧
↷  前进 1 秒
⏭  跳到末帧
```

实际界面使用 SVG 图标，不使用字体 Emoji。`1s`、`5f` 可以作为图标右下角的小徽标。

鼠标停留约 650 ms 后显示：

```text
上一帧
快捷键：← / A
```

其他 Tooltip：

```text
后退 5 帧
快捷键：Shift+← / Shift+A

后退 1 秒
快捷键：Ctrl+← / Ctrl+A

播放 / 暂停
快捷键：Space
```

还应增加常规方向键组合：

```text
Shift + Left / Right
Ctrl  + Left / Right
```

A/D 继续作为别名。

### 删除重复语义

建议删除顶部 `Step: 5/10` 设置和 Up/Down 快捷键。统一固定：

```text
普通逐帧：1 帧
快速逐帧：5 帧
时间跳转：1 秒
```

这样按钮、快捷键和帮助说明只有一套语义。

每个按钮应同时设置：

```qml
Accessible.name
Accessible.description
ToolTip.delay
ToolTip.text
```

---

## 6. Windows 资源管理器中选中两个视频，右键打开 Compare

可以实现，最终交互建议是：

```text
选中两个视频
    ↓
右键
    ↓
“使用 VCStation 对比”
    ↓
打开 VCStation
    ↓
显示同一个顺序 / Reference 确认界面
```

当前 MSI 只注册了 `.dvsproj` 文件关联，并没有注册视频文件的 Shell verb；GUI 启动参数也只支持单个 `.dvsproj`，其他普通视频参数会被拒绝。

Windows 的 `MultiSelectModel=Player` 专门用于支持多文件选择的 Shell verb；`IExplorerCommand::Invoke()` 可以直接接收包含所有选中文件的 `IShellItemArray`。([Microsoft Learn][2])

### 不建议只写一个简单 Registry command

简单的：

```text
VCStation.exe "%1"
```

无法可靠表达两个路径、选择数量、Unicode、混合扩展名和启用条件。

建议新增一个很小的 Shell 扩展：

```text
VCStationShell.dll
```

它只负责：

* 实现 `IExplorerCommand`；
* 读取 `IShellItemArray`；
* 仅在恰好选中两个本地视频时启用；
* 使用 `CreateProcessW` 启动 VCStation；
* 不加载 Qt；
* 不加载 FFmpeg；
* 不 probe 视频；
* 不访问网络。

Microsoft 明确指出 `IExplorerCommand` 方法运行在 Explorer UI 线程，因此不应执行网络或耗时操作。([Microsoft Learn][3])

Shell 扩展启动：

```text
VCStation.exe --compare "a.mp4" "b.mp4"
```

应用侧新增类型化启动协议：

```cpp
struct StartupRequest {
    enum class Kind {
        Empty,
        OpenProject,
        PlaySingle,
        Compare,
    };

    Kind kind;
    std::vector<std::filesystem::path> sources;
};
```

支持：

```text
VCStation.exe --play "video.mp4"
VCStation.exe --compare "a.mp4" "b.mp4"
VCStation.exe --compare "a.mp4" "b.mp4" "c.mp4"
VCStation.exe "review.dvsproj"
```

GUI 当前使用 `main(int, char**)` 直接读取窄字符参数。资源管理器集成后，应改为通过 `QCoreApplication::arguments()` 取得 `QStringList`，再转换成宽字符 `std::filesystem::path`，否则中文或特殊字符路径可能在 Windows 本地代码页转换中受损。

### 单实例转发

为了避免 VCStation 已经打开时又启动第二个进程，建议加入：

```text
Primary VCStation
    QLocalServer / named pipe

Secondary VCStation
    发送 StartupRequest JSON
    退出
```

主实例收到请求后切到前台，并显示确认框。

Windows 11 的新式右键菜单是否直接显示第三方命令需要真实验证；第一版可以接受它出现在“显示更多选项”中，但 Win10、Win11 都要加入安装测试。

---

# 二、当前项目仍存在的整体技术不足

## 1. 核心模型仍把“会话”写死为“比较”

当前至少两路的约束分布在 domain、application、provider、UI 和 persistence 多层，说明“源数量”没有被设计成真正的会话属性。单视频功能会迫使这些分散不变量同时修改。

下一阶段应统一成：

```text
ReviewSession
├── sourceCount: 1～3
├── canonicalSource
├── optional reference
├── optional comparison edge
└── derived mode: Single / Comparison
```

不建议再增加类似：

```text
SingleVideoController
SingleVideoProvider
SingleVideoSurface
```

否则会重新形成两套播放体系。

---

## 2. QML 与 Renderer 缺少共享几何契约

Wipe 不一致就是典型结果：

* QML 自己计算线的位置；
* Renderer 自己计算视频内容位置；
* 两边使用不同坐标空间。

后续还会影响：

* ROI 框；
* 面板标签；
* Difference unavailable overlay；
* 点击像素取样；
* 未来放大镜。

应新增共享的 geometry helper，例如：

```cpp
struct SurfaceLayout {
    std::array<SurfaceRect, 4> panels;
    SurfaceRect contentRect;
    float wipeSplitX;
};
```

由 C++ 根据：

```text
view mode
logical size
physical size
source geometry
DPR
wipe position
```

统一计算。Renderer 使用该结果，QML 通过 `ComparisonSurface` 暴露只读 geometry property 使用同一结果。

---

## 3. `Main.qml` 已承担过多职责

目前一个文件同时包含：

* 菜单；
* 文件对话框；
* 拖放；
* 确认框；
* source cards；
* comparison toolbar；
* alignment inspector；
* viewport；
* Wipe；
* ROI；
* Loading；
* timeline；
* transport；
* Bad Case 导出。

这使得一个确认框的类型冲突或 Popup 样式问题很容易影响整个窗口，也导致现有 contract test只能做浅层实例化检查。

应拆分为：

```text
Main.qml
├── SourceBar.qml
├── DropOverlay.qml
├── DropConfirmationDialog.qml
├── ComparisonToolbar.qml
├── AlignmentInspector.qml
├── ComparisonViewport.qml
├── WipeHandle.qml
├── TimelineBar.qml
├── TransportBar.qml
└── WorkspaceDialogs.qml
```

v1.1.1 至少应先拆出：

```text
DropConfirmationDialog.qml
WipeHandle.qml
TransportBar.qml
```

---

## 4. 项目持久化模型落后于现有视图能力

当前 `ProjectViewLayout` 只有：

```text
SideBySide
ThreeUp
ReferenceFocus
Difference
```

没有：

```text
AnalysisGrid
Wipe
Single
```

`differenceEdge` 还是必填的两元素数组，因此无法表达单视频项目。

而 UI 偏好已经支持 Wipe 和 AnalysisGrid；Workspace 保存时会把 AnalysisGrid 折叠成 ThreeUp/SideBySide，Wipe 也不能被准确恢复。

v1.2.0 应升级为 Project Schema v4：

```cpp
enum class ProjectViewLayout {
    Single,
    SideBySide,
    ThreeUp,
    ReferenceFocus,
    Difference,
    AnalysisGrid,
    Wipe,
};

struct ProjectViewState {
    ProjectViewLayout layout;
    std::optional<std::array<SourceId, 2>> comparisonEdge;
    DifferenceMetric metric;
    DifferenceFilter filter;
    uint8_t gain;
    float wipePosition;
    bool thresholdEnabled;
    float threshold;
    ViewTransform viewport;
    optional<NormalizedRect> roi;
};
```

并实现 v3 → v4 迁移。

---

## 5. Workspace UI 仍以 16 ms 轮询状态

ReviewController 已经改成事件驱动，但 WorkspaceController 仍使用 16 ms QTimer 读取 workspace snapshot。

应为 WorkspaceCoordinator 增加与 PlaybackCoordinator 相同的：

```text
statePublished callback
→ queued invoke
→ WorkspaceController.refresh()
```

删除常驻 16 ms 轮询。

---

## 6. Provider 仍会在单个 FrameSet 内阻塞等待全部 source

SourceDecodeActor 已经使用 callback 发布结果，但 provider worker 仍在：

```cpp
for (...) {
    completionMailbox->take();
}
```

中等待全部 source 完成。

对当前 1～3 路短视频，这不是阻断项，但从架构上仍不是真正的非阻塞 aggregator。后续应改为：

```text
actor completion
    ↓
provider completion queue
    ↓
pendingSets[operationId]
    ↓
所有 slot 完成
    ↓
publish FrameSet
```

这样 open、close、cancel 不会等待当前 FrameSet 的慢 source。

---

## 7. 下一版本必须恢复签名

v1.1.0 的 Release workflow包含一个明确的“仅允许 v1.1.0 无签名发布”的例外，并会拒绝其他版本继续沿用该例外。

v1.1.1 或 v1.2.0 必须恢复：

* `VCStation.exe` 签名；
* `VCStationCli.exe` 签名；
* `VCStationShell.dll` 签名；
* MSI 签名；
* 签名后再计算 SHA-256。

尤其是 Shell 扩展会由 Explorer 加载，不能继续使用无签名发布策略。

---

# 三、建议的具体实施路线

## 阶段 A：v1.1.1 交互与可靠性补丁

### A1. 清除所有活动 4K 测试

完成：

```text
删除 performance.4k30-main10
删除 4K Profile 参数
删除 4K fixture 要求
删除 workflow exclude-regex
更新 runner 文档
更新测试策略
```

仅历史 Release Notes 可以保留曾经测试过 4K 的记录。

### A2. 修复确认框

完成：

* QtQuick.Dialogs 使用 import alias；
* 新建 `DropConfirmationDialog.qml`；
* `Popup.Item`；
* `Overlay.overlay`；
* 不透明背景；
* 自定义 footer；
* WARP 像素回归测试。

### A3. 修复 Wipe

完成：

* `wipePosition` 统一为 surface normalized coordinate；
* Renderer 从 surface splitX 推导 content UV；
* QML 鼠标映射到 `dualVideoSurface`；
* 手柄增大；
* `DragHandler`；
* 25/50/75% 像素一致性测试。

### A4. 重做 Transport

完成：

* 新建 `TransportBar.qml`；
* SVG 图标；
* 统一 1 帧、5 帧、1 秒；
* 650 ms Tooltip；
* 显示快捷键；
* 增加 Shift/Ctrl + 方向键；
* 删除 Step 5/10 和 Up/Down 重复语义。

### A5. 恢复发布签名

删除 v1.1.0 unsigned exception，恢复标准 Authenticode 流程。

---

## 阶段 B：v1.2.0 单视频与 Windows 集成

### B1. 1～3 路统一模型

依次修改：

```text
ComparisonValidator
FrameSet
SessionSnapshot
MultiSourceFrameProvider
PlaybackCoordinator
ReviewController
ComparisonSurface
D3d11ComparisonRenderer
Project
ProjectJson
WorkspaceCoordinator
```

### B2. 单视频 UI

单视频模式下：

```text
显示：
    视频画面
    Timeline
    首帧 / 上一帧 / 播放 / 下一帧 / 末帧
    Zoom / Pan / ROI
    Bad Case 导出

隐藏：
    Reference 选择
    Compare pair
    Diff
    Wipe
    Alignment
    Offset
    Anchors
```

空窗口提示改为：

```text
拖入 1～3 个视频，或一个 .dvsproj 项目
```

文件选择器也改为：

```text
打开 1～3 个视频
```

### B3. 启动请求协议

新增：

```text
StartupRequest.h
StartupRequest.cpp
StartupRequestParserTests.cpp
```

所有 GUI 参数通过 Unicode `QStringList` 解析。

### B4. Explorer 右键集成

新增轻量目标：

```text
src/shell_windows/
├── ExplorerCommand.cpp
├── ExplorerCommand.h
├── ClassFactory.cpp
└── VCStationShell.def
```

MSI 注册：

```text
SystemFileAssociations
supported extensions
ExplorerCommandHandler
MultiSelectModel=Player
```

只在选中恰好两个支持的视频时启用：

```text
使用 VCStation 对比
```

### B5. 单实例转发

新增：

```text
StartupRequestBroker
├── primary QLocalServer
├── secondary forward
└── bounded JSON protocol
```

---

# 四、更新后的验收标准

## 不再测试 4K

* 活动代码中不存在 `4k30-main10` Profile；
* workflow 不再 exclude 4K，而是根本没有 4K 测试；
* runner 不需要 4K fixture；
* 保留小分辨率 10-bit/P010 正确性测试。

## 确认框

* 拖入两个或三个视频后确认框完全不透明；
* 背景正确 dim；
* 100%、125%、150% DPI 正常；
* OK/Cancel 可见；
* 没有 QML import ambiguity。

## 单视频

* 拖入一个视频后直接打开；
* 第一帧正确显示；
* 播放、暂停、seek、逐帧正常；
* 单视频占满 viewport；
* 对比和对齐按钮隐藏；
* 单视频项目可保存和恢复。

## Wipe

* 手柄可见尺寸至少 44×60；
* 命中区域至少 64 px；
* 手柄与视频切分误差不超过一个物理像素；
* letterbox、DPI、ROI、zoom 下仍一致。

## Transport

* 不再显示裸 `-1`、`-5`、`+1`；
* 所有按钮使用统一图标；
* Tooltip 包含动作和快捷键；
* 快捷键语义只有 1 帧、5 帧、1 秒三档；
* 960 像素窗口下无裁切。

## Explorer

* 选中两个视频时出现“使用 VCStation 对比”；
* 一个或三个以上文件时按定义隐藏或禁用；
* 中文、空格和长路径正常；
* 打开后进入顺序/Reference 确认框；
* 卸载后 Shell verb 与 COM 注册完全清理；
* Win10 和 Win11 均验证。

---

# 最终优先级

下一步建议严格按以下顺序推进：

> **彻底移除 4K 测试 → 修复透明确认框 → 修正 Wipe 坐标与手柄 → 重做 Transport → 发布 v1.1.1 → 泛化 1～3 路会话 → 单视频模式 → StartupRequest → Explorer 右键集成 → Schema v4 → 发布 v1.2.0。**

其中，透明确认框和 Wipe 坐标属于明确的现有缺陷；单视频和 Explorer 右键则属于跨层能力扩展。拆成两个版本可以避免把高风险的领域模型变更与紧急 UI 修复绑定在同一次发布中。

[1]: https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html "https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html"
[2]: https://learn.microsoft.com/en-us/windows/win32/shell/how-to-employ-the-verb-selection-model "https://learn.microsoft.com/en-us/windows/win32/shell/how-to-employ-the-verb-selection-model"
[3]: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorercommand "https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorercommand"
