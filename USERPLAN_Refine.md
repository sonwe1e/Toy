# 核心结论

上一版路线需要做一次结构性调整：不再把“单视频播放”“多视频比较”“隐藏工具栏”“全屏”视为彼此独立的功能，而是统一成一个 **1～3 路视频审查画布**。

新的产品形态应是：

```text
1 路视频
→ 单画面铺满整个画布

2 路视频
→ 默认左右两栏铺满整个画布

3 路视频
→ 默认三栏铺满整个画布

沉浸模式
→ 隐藏菜单、Source 栏、比较工具栏、时间轴和按钮
→ 视频画布占满窗口或整个显示器
→ 快捷键继续有效
→ Wipe 手柄继续可拖动
```

实现上最关键的不是再往 `Main.qml` 增加几个 `visible` 条件，而是建立三个清晰的基础模型：

1. **源数量决定有效画面布局**；
2. **画布内容与外围工具栏完全解耦**；
3. **按钮、快捷键和右键菜单共享同一套 Action，不依赖控件是否可见**。

---

# 一、修订后的交互设计

## 1. Wipe 手柄改为“窄而高”

你提出的方向是合理的：现在的手柄偏宽、偏短，看上去像普通按钮，而且精确选中比较困难。

建议将可见手柄调整为：

```text
可见宽度：18～22 px
可见高度：76～92 px
圆角：9～11 px
分割线：2～3 px
实际鼠标命中宽度：48～56 px
实际命中高度：覆盖整条分割线
```

也就是说，视觉上左右收窄、上下拉长，但实际鼠标可点击区域仍然较宽：

```text
     实际命中区域 52 px
┌──────────────────┐
│       │ 18 px    │
│       │          │
│       │ 可见手柄 │  84 px
│       │          │
│       │          │
└──────────────────┘
```

手柄内部不再使用横向 `↔`，建议改成三组竖向抓取点：

```text
•
•
•
```

这样视觉语义更接近“拖动分割线”，而不是“点击切换”。

### 操作行为

* 拖动整条分割线附近都能移动 Wipe；
* 双击手柄恢复到 50%；
* 鼠标进入手柄时稍微提高亮度；
* 拖动时手柄放大约 8%，强化当前抓取状态；
* 沉浸模式下仍保留手柄；
* 非 Wipe 模式下手柄完全不存在。

---

## 2. 单视频必须铺满整个画布

单视频模式不应该模拟“两栏布局中的左侧一栏”，而应当使用整个可用画布：

```text
┌──────────────────────────────────────┐
│                                      │
│                                      │
│            单视频完整画面             │
│                                      │
│                                      │
└──────────────────────────────────────┘
```

加入第二个视频后，再切换为：

```text
┌───────────────────┬──────────────────┐
│                   │                  │
│     Source A      │     Source B     │
│                   │                  │
└───────────────────┴──────────────────┘
```

加入第三个视频后：

```text
┌─────────────┬─────────────┬─────────────┐
│  Source A   │  Source B   │  Source C   │
└─────────────┴─────────────┴─────────────┘
```

这不是简单地改变 QML 宽度。当前核心验证器、`FrameSet`、Snapshot 和 Frame Provider 都把“两路”作为最小数量，因此需要将底层会话正式泛化为 1～3 路。

---

## 3. 新增视频时采用“会话重建”，不要热插 Decoder

单视频正在播放时加入第二个视频，不建议直接往现有 `MultiSourceFrameProvider` 中热插一个 `SourceDecodeActor`。这会显著增加：

* 播放中途的生命周期竞争；
* 当前 FrameSet 与新 source 数量不一致；
* cancellation 和 generation 管理复杂度；
* GPU mailbox 中旧单路帧与新双路帧混合的风险。

建议使用受控的原子重建流程：

```text
单视频正在显示 frame 824
        ↓
用户加入第二个视频
        ↓
暂停播放
        ↓
记录当前 MediaTime 和 ViewTransform
        ↓
确认 A/B 顺序与 Reference
        ↓
关闭旧单路 provider session
        ↓
用 A/B 创建新的双路 session
        ↓
在新 canonical timeline 中寻找同一 MediaTime
        ↓
显示对应 FrameSet
        ↓
默认进入 Side-by-side
```

这样仍然复用现有 session epoch 和 playback generation 机制，不需要让一个活动 provider 动态改变 source 数量。

### 为什么保存 MediaTime，而不是只保存 FrameId

假设单视频 A 是 120 FPS，而新加入的 B 是 60 FPS，或者用户把 B 改成 Reference，原来的 frame 824 不一定仍对应同一时刻。

应保存：

```cpp
struct ReviewResumePoint {
    domain::MediaTime mediaTime;
    bool wasPlaying;
    SurfaceViewTransform transform;
    std::optional<SurfaceNormalizedRect> roi;
};
```

新 session 打开后，根据新 canonical timeline 找到最接近该时间的位置。

加入新视频后建议默认保持暂停，避免用户还没理解新的左右顺序就自动继续播放。

---

# 二、统一画面布局模型

## 1. 不单独维护“单视频模式”开关

不要增加一个容易失配的：

```cpp
bool singleVideoMode;
```

模式应直接从 source 数量推导：

```cpp
enum class ReviewContentMode {
    Empty,
    SingleSource,
    Comparison,
};

ReviewContentMode contentMode(std::size_t sourceCount) {
    if (sourceCount == 0) {
        return ReviewContentMode::Empty;
    }
    if (sourceCount == 1) {
        return ReviewContentMode::SingleSource;
    }
    return ReviewContentMode::Comparison;
}
```

这样不会出现：

```text
singleVideoMode = true
但 sources.size() = 2
```

这种状态冲突。

---

## 2. 新增 Single View，但保留旧枚举数值

当前 ViewMode 已经包含 Side-by-side、Three-up、Difference、Analysis Grid 和 Wipe。

新增 Single 时必须放在最后并使用显式数值，避免旧设置中的整数改变含义：

```cpp
enum class SurfaceViewMode : std::uint8_t {
    SideBySide     = 0,
    ThreeUp        = 1,
    ReferenceFocus = 2,
    Difference     = 3,
    AnalysisGrid   = 4,
    Wipe           = 5,
    Single         = 6,
};
```

有效模式由 source 数量与用户偏好共同决定：

```cpp
SurfaceViewMode effectiveViewMode(
    std::size_t sourceCount,
    SurfaceViewMode requestedMode,
    SurfaceViewMode lastComparisonMode) {

    if (sourceCount == 1) {
        return SurfaceViewMode::Single;
    }

    if (requestedMode == SurfaceViewMode::Single) {
        return lastComparisonMode;
    }

    return requestedMode;
}
```

这意味着：

* 只有一个视频时强制完整单画面；
* 加入第二个视频后恢复用户上一次使用的比较模式；
* 默认上一次模式为空时使用 Side-by-side；
* 从两个视频退回一个视频时，原比较模式仍然记住。

---

## 3. Renderer 中的 Single 布局

在 `D3d11ComparisonRenderer::prepareSetDraw()` 中增加：

```cpp
if (state.viewMode == SurfaceViewMode::Single) {
    const GpuFrameResource* frame = firstAvailableFrame();

    appendRegionDraw(
        SurfaceRect{
            .x = 0.0F,
            .y = 0.0F,
            .width = state.logicalWidth,
            .height = state.logicalHeight,
        },
        *frame,
        *backing
    );
}
```

Renderer 仍然执行 aspect fit，因此“铺满整个画布”指的是使用整个画布作为可用边界，而不是强行拉伸视频破坏比例：

```text
画布比例与视频一致
→ 视频真正填满

画布比例与视频不同
→ 保持比例，剩余区域为黑色 letterbox
```

当前 GPU 层的 `GpuFrameSet` 并没有要求至少存在两个 GPU slot，因此单路 GPU 上传本身不需要另起一套实现。

---

# 三、增加“沉浸式审查模式”

## 1. 区分隐藏工具栏和操作系统全屏

建议提供两个彼此独立、但可以组合的状态：

```cpp
struct PresentationShellState {
    bool chromeVisible = true;
    bool fullScreen = false;
};
```

对应交互：

| 操作      | 行为             |
| ------- | -------------- |
| `Tab`   | 显示或隐藏应用工具栏     |
| `F11`   | 进入或退出真正的显示器全屏  |
| `Esc`   | 退出全屏；非全屏时恢复工具栏 |
| 鼠标移动到顶部 | 可选地临时显示简化顶部栏   |
| 鼠标移动到底部 | 可选地临时显示播放控制栏   |

这样支持三种使用方式：

```text
普通窗口 + 完整工具栏
普通窗口 + 纯画布
全屏 + 纯画布
```

不建议把 `F11` 与“隐藏工具栏”永久绑定。用户可能需要在全屏状态下临时按 `Tab` 调出工具栏调整比较对象或 Diff 参数。

---

## 2. 沉浸模式下保留哪些元素

全部隐藏：

* Window MenuBar；
* Source A/B/C 卡片；
* Comparison toolbar；
* Alignment Inspector；
* Timeline；
* Transport 按钮；
* 永久 source 标签；
* 快捷键说明文字。

继续保留：

* 视频画布；
* Wipe 分割线和手柄；
* 首次打开 Loading；
* 严重错误提示；
* 延迟帧的小型 indicator；
* 临时播放/暂停反馈；
* 临时 Frame 编号反馈。

例如按一次右方向键后，在画面底部短暂显示：

```text
Frame 825 / 3600
```

约 800 ms 后淡出。这样不需要永久时间轴，用户仍知道当前位置。

---

## 3. QML 布局不要继续锚定到工具栏对象

当前 viewport 的上、下、右边界直接依赖比较栏、Transport 和 Alignment Inspector。

建议改成一个永远占满窗口的 `contentHost`：

```qml
Item {
    id: contentHost
    anchors.fill: parent

    ComparisonViewport {
        anchors.fill: parent

        anchors.topMargin:
            root.chromeVisible
            ? sourceBar.height + comparisonBar.height
            : 0

        anchors.bottomMargin:
            root.chromeVisible
            ? transportBar.height
            : 0

        anchors.rightMargin:
            root.chromeVisible && root.inspectorOpen
            ? alignmentInspector.width
            : 0
    }
}
```

各工具栏作为独立 chrome 层存在：

```qml
SourceBar {
    visible: root.chromeVisible
}

ComparisonToolbar {
    visible: root.chromeVisible
}

TransportBar {
    visible: root.chromeVisible
}

AlignmentInspector {
    visible: root.chromeVisible && root.inspectorOpen
}
```

隐藏 chrome 后 margin 自动归零，画布真正扩展至窗口四边，不留下空白占位。

---

# 四、快捷键必须与按钮显示状态解耦

当前部分 Shortcut 会检查底部 Button 的 `enabled`，并通过 `button.click()` 执行命令。

这种方式在工具栏被 Loader 卸载或按钮隐藏后容易失效。

## 推荐使用统一 Action 层

新建：

```text
qml/actions/ReviewActions.qml
```

例如：

```qml
QtObject {
    id: actions

    readonly property bool canPrevious:
        root.graphicsReady
        && !root.busy
        && root.controller
        && root.controller.canPrevious

    function previousFrame() {
        if (canPrevious)
            root.controller.previous()
    }

    function nextFrame() {
        if (canNext)
            root.controller.next()
    }

    function stepBackwardFive() {
        if (canPrevious)
            root.controller.stepFrames(-5)
    }

    function togglePlayback() {
        if (root.controller)
            root.controller.togglePlayback()
    }
}
```

按钮调用：

```qml
onClicked: actions.previousFrame()
```

快捷键调用：

```qml
Shortcut {
    sequence: "Left"
    enabled: actions.canPrevious
    onActivated: actions.previousFrame()
}
```

右键菜单也调用：

```qml
MenuItem {
    text: qsTr("Previous frame")
    onTriggered: actions.previousFrame()
}
```

因此无论 Transport 是否显示，所有操作都走同一条路径。

---

## 建议保留的快捷键

考虑你明确提到左右和上下仍要有效，建议保持兼容：

| 快捷键                     | 操作         |
| ----------------------- | ---------- |
| `Left` / `A`            | 上一帧        |
| `Right` / `D`           | 下一帧        |
| `Down` / `Shift+A`      | 后退 5 帧     |
| `Up` / `Shift+D`        | 前进 5 帧     |
| `Ctrl+Left` / `Ctrl+A`  | 后退约 1 秒    |
| `Ctrl+Right` / `Ctrl+D` | 前进约 1 秒    |
| `Space`                 | 播放或暂停      |
| `Home`                  | 首帧         |
| `End`                   | 末帧         |
| `Tab`                   | 显示或隐藏工具栏   |
| `F11`                   | 全屏         |
| `Esc`                   | 退出全屏或恢复工具栏 |

Wipe 仍使用鼠标拖动。还可以增加：

```text
Alt + Left  → Wipe 向左移动 1%
Alt + Right → Wipe 向右移动 1%
Shift + Alt + Left/Right → 移动 5%
```

这样在沉浸模式下也能精确调整 Wipe，而不会占用原本的逐帧快捷键。

---

# 五、Wipe 坐标的最终修正方案

当前 Wipe 不一致的根源仍然是：

* QML 按整个 viewport 宽度计算手柄；
* Renderer 按视频 aspect-fit 后的内容矩形计算分割。

修订后的约定应是：

> `wipePosition` 表示完整 `ComparisonSurface` 中的归一化横坐标，而不是视频内容矩形中的横坐标。

## Renderer 计算

```cpp
const float surfaceSplitX =
    state.logicalWidth * std::clamp(
        state.wipePosition,
        0.0F,
        1.0F
    );

const float normalizedContentSplit =
    std::clamp(
        (surfaceSplitX - destination.x)
            / destination.width,
        0.0F,
        1.0F
    );
```

实际绘制：

```cpp
leftDestination.width =
    std::max(0.0F, surfaceSplitX - destination.x);

rightDestination.x =
    std::max(destination.x, surfaceSplitX);

rightDestination.width =
    std::max(
        0.0F,
        destination.x
            + destination.width
            - rightDestination.x
    );
```

UV 裁切使用 `normalizedContentSplit`。

## QML 计算

```qml
function setWipeFromSceneX(sceneX) {
    const local = viewportFrame.mapToItem(
        dualVideoSurface,
        sceneX,
        0
    );

    root.wipePosition = Math.max(
        0,
        Math.min(
            1,
            local.x / dualVideoSurface.width
        )
    );
}
```

最理想的下一步是由 C++ 暴露实际分割坐标：

```cpp
Q_PROPERTY(
    qreal wipeSplitLogicalX
    READ wipeSplitLogicalX
    NOTIFY presentationGeometryChanged
)
```

QML 手柄只读取这个属性，不再自己重复计算 Renderer 几何。这能彻底避免 DPI、边距、ROI 和窗口缩放造成的偏差。

---

# 六、单视频加入第二个视频后的 UI 状态机

建议明确以下转换：

## Empty → Single

```text
拖入一个视频
→ 直接打开
→ 显示第一帧
→ Single View
→ 完整画布
```

不再显示“还需要再拖一个视频”的错误提示。当前逻辑只会把单个文件选为 A，并提示继续拖入，尚未打开视频。

## Single → Comparison

```text
当前单视频 A
+ 拖入视频 B
→ 显示 Compare Confirmation
→ 默认 A 为 Reference
→ 保留当前时间位置
→ 切换 Side-by-side
```

## Comparison → Three-source

```text
A + B
+ 拖入 C
→ 确认顺序
→ 保持现有 A/B 顺序
→ 默认 C 追加在末尾
→ 默认切换 Three-up
```

## Comparison → Single

从 Source 卡片移除 B/C 后：

```text
只剩 A
→ 自动切换 Single
→ 保留当前时间和画面变换
```

不应要求关闭当前会话再手动重新打开。

---

# 七、项目持久化需要同步升级

当前 Project View 枚举不包含 Single、Wipe 和 Analysis Grid，difference edge 又始终要求两个 source。

建议在 v1.2.0 升级为 Schema v4：

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

    std::optional<
        std::array<SourceId, 2>
    > comparisonEdge;

    ProjectDifferenceMetric metric;
    ProjectDifferenceFilter filter;

    std::uint8_t gain = 1;
    float wipePosition = 0.5F;

    bool thresholdEnabled = false;
    float threshold = 0.0F;

    SurfaceViewTransform transform;

    std::optional<
        SurfaceNormalizedRect
    > roi;
};
```

单视频项目：

```text
layout = Single
comparisonEdge = nullopt
```

双视频 Wipe 项目：

```text
layout = Wipe
comparisonEdge = [0, 1]
wipePosition = 0.37
```

全屏和工具栏隐藏状态不建议写入项目。否则用户双击项目后可能意外直接进入无工具栏全屏。它们应该属于临时窗口状态或全局用户偏好。

---

# 八、文件级实施方案

## v1.1.1：沉浸模式和 Wipe 修复

```text
src/ui_qml/qml/
├── Main.qml
├── ComparisonViewport.qml
├── WipeHandle.qml
├── TransportBar.qml
├── ReviewActions.qml
└── DropConfirmationDialog.qml
```

修改内容：

```text
Main.qml
    增加 chromeVisible
    增加 fullScreen
    增加 Tab / F11 / Esc
    不再让快捷键依赖按钮

WipeHandle.qml
    视觉 20 × 84
    命中区域 52 px
    DragHandler
    双击复位

ComparisonViewport.qml
    chrome 隐藏时 anchors.fill
    Wipe 在沉浸模式下保持可见

D3d11ComparisonRenderer.cpp
    修复 surface/content 两套坐标
```

---

## v1.2.0：1～3 路统一会话

```text
src/domain/
    ComparisonValidator
    Project
    ProjectJson Schema v4

src/application/
    FrameSet
    SessionSnapshot
    PlaybackCoordinator
    ReviewResumePoint

src/media_ffmpeg/
    MultiSourceFrameProvider
    FrameSetAssembler

src/platform_windows/
    D3d11ComparisonRenderer
    SurfaceViewMode::Single

src/ui_qml/
    ReviewController
    SourceBar
    ComparisonToolbar
    Main
```

新增核心入口：

```cpp
bool ReviewController::openSources(
    const QList<QUrl>& sources,
    int referenceIndex,
    std::optional<domain::MediaTime> resumeTime
);
```

不要再继续扩展：

```cpp
openComparisonSet(
    first,
    second,
    third,
    referenceIndex
)
```

因为这种固定参数 API 与 1～3 路模型不匹配。

---

# 九、测试与验收矩阵

## Wipe

必须覆盖：

```text
25%、50%、75%
16:9、9:16、不同分辨率
100%、125%、150% DPI
普通窗口、隐藏工具栏、全屏
ROI、Zoom、Pan
A/B、A/C、B/C
```

验收：

```text
手柄中心与实际视频切换边界
误差不超过 1 个物理像素
```

## 单视频

必须覆盖：

```text
拖入一个视频
文件选择器打开一个视频
命令行 --play
单视频播放
单视频逐帧
单视频 1080p60
单视频 1080p120
单视频 → 双视频
双视频 → 单视频
保存并恢复单视频项目
```

## 沉浸模式

必须覆盖：

```text
Tab 隐藏工具栏
Tab 恢复工具栏
F11 全屏
Esc 退出全屏
隐藏工具栏时所有快捷键有效
隐藏工具栏时 Wipe 可拖动
错误和 Loading 仍可见
```

---

# 最终实施顺序

修订后的顺序应为：

> **修复 Wipe 坐标 → 重做窄高型 Wipe 手柄 → 抽离 Action/Shortcut 层 → 实现工具栏隐藏 → 实现 F11 全屏 → 发布交互补丁 → 泛化 1～3 路会话 → 实现 Single View → 实现加入第二路时的受控会话重建 → 升级 Project Schema v4。**

其中最重要的架构原则是：

> **画面布局由 source 数量决定，外围工具栏只是可隐藏的外壳；快捷键和 Wipe 属于审查画布本身，不应依赖工具栏是否存在。**
