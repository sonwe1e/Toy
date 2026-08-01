# 核心结论

提交 `0b639491` 已经完成了明显的 **Player-first 界面重构**。相比上一版，界面不再是“三张 Source 卡片 + 多条永久工具栏”的工程控制台，而是形成了：

```text
空状态
→ 播放器式单视频
→ 上下文式多视频比较
→ Viewer 原生右键菜单
→ 悬浮播放控制器
→ 多轨审查时间轴
→ 可展开专业 Inspector
```

整体方向正确，界面架构完成度约 **85%**。空状态、单视频、双/三路模式的层级已经比较合理，默认窗口中的 Viewport 占比也达到测试设定的 78% 以上。

但当前仍存在几项会直接影响实际显示的缺陷：

1. 空状态会错误显示底部播放控制器；
2. 单视频 OSC 自动隐藏逻辑存在闪烁和隐形点击问题；
3. OSC 内部时间轴和播放按钮存在几何重叠；
4. Viewer 顶部 Source 标签、Diff 控件和状态信息会相互遮挡；
5. Frame Error Banner 实际可能被定位到 Viewport 之外；
6. 时间轴悬浮预览的帧号、Timecode 和缩略图状态没有同步；
7. 单视频右键打开 Inspector 后，Inspector 实际不会显示。

因此当前版本已经适合内部体验，但还不建议把这套界面视为最终稳定版。

---

# 一、本次修改完成得比较好的部分

## 1. 界面信息架构已经明显改善

当前主界面固定占用高度大致为：

```text
Command Bar        48 px
Active Source Strip 42 px
Compare Mode Bar   40 px（仅多视频）
```

多视频模式总计约 130 px，比上一版接近 300 px 的永久工具区明显精简。底部播放控制器改为覆盖在 Viewer 上，不再挤压 Viewport。代码测试也明确要求 Viewport 高度占窗口内容区至少 78%。

这已经接近成熟播放器和审片工具常见的“画面为主、操作浮层化”结构。

## 2. Active Source 与待打开 Source 已经分离

新的 `ReviewShellController` 区分：

```text
activeSources
    后端真正成功打开的 Source

stagedSources
    用户正在选择、但尚未成功应用的 Source
```

它还维护 canonical source、外部启动请求队列、界面显示状态和 dirty guard。失败的 Source 替换不会立即重绘当前活动 Source，避免出现“界面写着新文件，但画面仍是旧文件”的状态。

`ReviewController` 也已经向 QML 暴露 `activeSources`、`canonicalSourceIndex` 和 `referenceSourceIndex`，Reference 不再完全是 QML 自己猜测的局部状态。

这是本次最重要的正确性改进之一。

## 3. Source Strip 比旧三卡布局更适合播放器

当前 Source 以 Chip 形式显示：

```text
A · reference.mp4   R
B · prediction.mp4  …
C · prediction2.mp4 …
+
```

Reference/Canonical Source 具有不同底色和边框，右侧菜单可以切换 Reference 或移除 Source。单视频时只显示一个 Chip 和添加按钮，不再永久显示空的 B/C 卡片。

这已经符合之前提出的“单视频看起来应当是完整播放器，而不是尚未配置完的比较任务”。

## 4. Compare Mode Bar 的主次功能层级合理

Side、Wipe、Diff 作为高频命令直接显示；Three-up、Reference Focus、Analysis Grid 放入扩展菜单；三路模式才出现 Pair 选择。

这种层级比以前把 Reference、View、Metric、Gain、Pair、Filter 全部挤在横向 Flickable 中更容易理解。

## 5. Viewer 与 Renderer 使用了相同几何

`ComparisonSurface` 现在直接向 QML 暴露：

```text
wipeSplitLogicalX
sourcePanelRects
presentationGeometryChanged
```

Source 标签不再自行假设“左半边、右半边”，而是读取 Renderer 的真实布局。这对于 Three-up、Reference Focus、Analysis Grid 和 Wipe 非常重要。

对应测试已经验证三路面板、Reference Focus、Analysis Grid 和 Wipe Pair 的几何。

## 6. 快捷键、右键菜单和 Inspector 已形成完整入口

当前已经支持：

* Review / Player 两套快捷键方案；
* `?` 打开快捷键帮助；
* 右键访问 View、Pair、Reference、Bad Case、Inspector 和 Full Screen；
* `I/O` 设置 In/Out；
* `\` 播放选中区间；
* 双击 Viewer 切换全屏。

从功能可发现性上看，已经比上一版成熟很多。

---

# 二、当前最明显的界面显示缺陷

## P0-1：空状态仍会显示完整播放控制器

`oscState` 当前定义为：

```qml
!chromeVisible ? Hidden
: singleMode ? Auto
: Pinned
```

当 `sourceCount === 0` 时，`singleMode` 为 false，因此 OSC 会进入 `Pinned`。也就是说，空状态下虽然中央显示“Drop one to three videos”，底部仍会出现一个禁用的 Timecode、Timeline 和播放按钮区域。

这会破坏刚刚建立起来的干净空状态。

应改成：

```qml
readonly property int oscState:
    sourceCount === 0 ? 2
    : !chromeVisible ? 2
    : singleMode ? 1
    : 0
```

空状态只能显示：

```text
品牌栏
中央打开入口
拖放提示
```

不应显示任何禁用播放组件。

---

## P0-2：单视频 OSC 自动隐藏逻辑实际上会立即消失

`PlayerOsc` 在鼠标离开时执行：

```qml
pointerInside = false
hideTimer.restart()
```

但透明度直接依赖 `pointerInside`，因此鼠标一离开控制器，OSC 会立刻开始消失；900 ms Timer 最后只是再次将已经为 false 的属性设置为 false。

正确逻辑应当将“鼠标是否位于控制区”和“控制器当前是否显示”拆开：

```qml
property bool revealActive: controllerState === Pinned

onHoveredChanged: {
    if (hovered) {
        revealActive = true
        hideTimer.stop()
    } else {
        hideTimer.restart()
    }
}

Timer {
    interval: 1200
    onTriggered: revealActive = false
}
```

另外，当前 OSC 即使 `opacity: 0`，仍然保持 `visible: true`，其中的 Timeline MouseArea 和按钮仍可能接收事件。用户在看不到控制器时点击底部 94 px 区域，可能触发 seek。

建议增加独立的 8～12 px 底部唤醒热区，并在控制器隐藏时禁用内部交互：

```qml
controlsEnabled: opacity > 0.5
```

---

## P0-3：OSC 内部布局存在实际几何重叠

`PlayerOsc` 高度是 94 px，其中同时放置：

* 顶部 Timecode Readout；
* 42 px Timeline；
* 42 px TransportBar；
* 上下 margin。

TransportBar 又通过 `scale: 0.72` 进行视觉缩小，但 `scale` 不会改变 QML 布局占用尺寸。

按当前坐标估算：

```text
Readout             y ≈ 8–24
Timeline            y ≈ 26–68
Transport 原始区域   y ≈ 47–89
Transport 可见区域   y ≈ 53–83
```

Timeline 主轨和播放按钮会有约 14～20 px 的重叠，进度滑块可能被按钮遮挡。

不建议继续用 `scale` 压缩完整 TransportBar。应新增真正的 Compact Transport：

```text
按钮：38 × 34
图标：20 × 20
间距：4
```

PlayerOsc 建议改为 116～124 px：

```text
Readout        20 px
Timeline       38 px
Transport      36 px
Margins        18 px
```

或者将 Timecode 放到播放按钮左侧，使 Timeline 单独占一整行。

---

## P0-4：顶部标签和 Diff 控件会发生遮挡

`analysisChrome` 固定在 Viewer 右上角；Source 标签同样位于每个 Panel 的顶部，并且标签是后声明的 QML sibling。两者没有设置明确的 z 层级。

在 Side-by-side 模式中，右侧 Source 标签大约占据：

```text
右侧 Panel x + 12
到窗口右边 - 12
高度 46 px
```

Diff/Threshold 控件也位于右上角、高度 32 px，区域基本完全重合。Source 标签很可能盖住：

* Threshold；
* Exactness；
* Threshold code；
* Filter；
* Reset zoom；
* Clear ROI。

同样的问题也可能影响顶部 Alignment Status。

应建立统一 Viewer Overlay 层级：

```text
z = 10  Source badges
z = 20  Persistent status
z = 30  Analysis controls
z = 40  Toast / Frame pending
z = 50  Wipe handle
```

更推荐把大部分 Diff 参数移入 Compare Inspector，只在 Viewer 留下：

```text
Threshold 开关
Exactness 状态徽标
Reset View 图标
```

而不是保留一整排高宽度控件。

---

## P0-5：Frame Error Banner 的锚点已经失效

`surfaceLabels` 当前是一个 `anchors.fill: parent` 的 Item。

但 `frameErrorBanner` 仍然使用：

```qml
anchors.top: surfaceLabels.bottom
```

由于 `surfaceLabels.bottom` 就是整个 Viewport 的底边，Error Banner 会被放到 Viewport 底部之外。

也就是说，在已有旧帧的情况下，如果下一帧读取失败，本来应该显示的：

```text
Frame unchanged
<具体错误>
```

很可能完全不可见。

应改成统一顶部 Overlay Stack，例如：

```qml
Column {
    id: topStatusStack
    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    topPadding: 12
}
```

Source badge、Alignment warning、Frame error 都由同一个布局容器管理，禁止彼此使用不相关组件的 `bottom` 作为锚点。

---

## P0-6：Wipe 靠近边缘时 Source 标签会互相覆盖

Wipe 模式的 `sourcePanelRects` 会按照分割位置返回左右区域。Source 标签宽度却使用：

```qml
Math.max(80, panelWidth - 24)
```

当 Wipe 位于 5% 或 95% 附近时，窄侧 Panel 可能只有 40～60 px，但标签仍然至少 80 px，因此会越过 Wipe 分割线，与另一侧标签重叠。

建议：

```text
Panel ≥ 160 px
    显示字母 + 文件名

Panel 80～159 px
    只显示字母

Panel < 80 px
    隐藏标签，保留边缘 Source badge
```

对于 Wipe，最稳妥的是在 Viewport 左上和右上固定显示 A/B Badge，不让标签宽度跟随分割区域变化。

---

# 三、时间轴当前存在的显示与同步问题

## 1. Hover Frame 与 Timecode/缩略图没有同步

`Main.qml` 将以下属性传入 `PlayerOsc`：

```qml
previewFrame: root.timelinePreviewFrame
previewTimecode: root.previewTimecode
previewThumbnailSource:
    thumbnailCache.urlForFrame(root.timelinePreviewFrame)
```

但 `PlayerOsc` 收到 Timeline 的 hover 后，直接执行：

```qml
control.previewFrame = frame
```

这会破坏原来的属性绑定，而且不会更新 `root.timelinePreviewFrame`。

结果可能是：

```text
Popup 显示 Frame 850
Timecode 仍为 00:00:00:00
缩略图仍是 Frame 0 或空白
```

应改为信号：

```qml
signal previewRequested(int frame)

TimelineTracks {
    onPreviewRequested:
        control.previewRequested(frame)
}
```

Main 中统一更新：

```qml
onPreviewRequested: frame => {
    timelinePreviewFrame = frame
}
```

`PlayerOsc` 不应修改一个由外部绑定进来的属性。

## 2. 拖动 Timeline 时反而隐藏缩略图

当前 Popup 条件是：

```qml
visible: tracks.hoverFrame >= 0 && !tracks.dragging
```

因此用户按住鼠标精确拖动时，缩略图会消失。

专业审片工具通常正是在拖动时持续显示 Frame/Timecode/Thumbnail。应改为：

```qml
visible: tracks.hoverFrame >= 0
```

拖动状态可以改变边框颜色，而不是隐藏预览。

## 3. 缩略图捕获了整个 Viewer UI，而不是纯视频画面

`TimelineThumbnailCache.sourceItem` 当前设置为：

```qml
viewportFrame
```

而 `viewportFrame` 内包含：

* Source 标签；
* Diff 控件；
* Frame Pending；
* Error Overlay；
* Alignment 状态。

生成的缩略图可能带有这些 UI 覆盖物。缓存实现本身会使用 `grabToImage()` 捕获传入 Item。

应改为：

```qml
sourceItem: viewportFrame.surface
```

保证缩略图只包含 Renderer 输出。

## 4. Timeline Zoom 后 In/Out 区域可能画出边界

In/Out Range 的 x 做了最小值限制，但 width 没有根据当前 Zoom Window 裁剪；`TimelineTracks` 本身也没有 `clip: true`。

当 In/Out 位于当前可见窗口之外时，Range 条可能绘制到 Timeline 外部。

应先计算：

```text
visibleIn  = clamp(inFrame, windowStart, windowEnd)
visibleOut = clamp(outFrame, windowStart, windowEnd)
```

并设置：

```qml
clip: true
```

## 5. 三条 Marker Track 没有图例

当前 Missing、Duplicate、Extra、Anchor 分布在不同高度，但用户无法从界面知道每一行代表什么。

至少需要 hover 信息：

```text
Source B · Missing frame
Frame 840
Confidence 12%
```

Inspector Review 页中还应提供固定颜色图例。

---

# 四、单视频模式仍有几个体验问题

## 1. 单视频 Inspector 实际无法显示

右键菜单始终提供：

```text
Inspector and media info
```

包括单视频模式。

但 Main 中 Inspector 的显示条件是：

```qml
visible:
    chromeVisible
    && inspectorOpen
    && !singleMode
```

所以单视频右键点击 Inspector 后，状态会变成打开，但界面没有任何变化。

正确设计应是：

```text
单视频：
    Review
    Info

多视频：
    Compare
    Alignment
    Review
    Info
```

不能直接隐藏整个 Inspector。

## 2. 单视频 Source Chip 的 “R” 语义不够清晰

单视频只有一个 Source，它必然是 canonical timeline source。此时仍显示一个可点击的 `R` 按钮；打开后，“Use as reference”和“Remove source”都会被禁用。

这会形成一个没有有效操作的菜单。

单视频应：

* 将 `R` 改为不可点击的 `SOURCE` 或 `TIMELINE` Badge；
* 或将菜单改为 Source Info / Replace Video；
* 不显示全部禁用的菜单。

## 3. Source 信息重复显示

单视频模式同时存在：

* Active Source Strip 文件名；
* Viewer 顶部近乎整行的 Source 标签。

这会重复占用空间。Viewer Label 更适合缩小为：

```text
A
```

或完全隐藏，鼠标进入画面时再临时显示。

---

# 五、多视频模式的进一步问题

## 1. Pair 在无关模式下仍然显示

三路视频时，Pair Combo 始终可见，即使当前是 Side-by-side 或 Three-up。

但 Pair 只真正影响：

* Wipe；
* Diff；
* Analysis Grid。

在 Side-by-side 中显示 Pair，会让用户误以为它会改变左右两栏。

建议：

```qml
visible:
    sourceCount === 3
    && (wipeMode || differenceMode || analysisGridMode)
```

## 2. 高级模式没有明确的当前状态

当用户通过 `…` 选择 Three-up、Reference Focus 或 Analysis Grid 后：

* Side/Wipe/Diff 三个按钮都不会选中；
* `…` 按钮也没有 Active Indicator；
* MenuItem 没有 check mark。

建议：

```text
… · Three up
```

或者给 `…` 按钮增加 Accent Dot，并让 MenuItem 具有 checked 状态。

## 3. Reference Focus 和 Analysis Grid 缺少显式分隔线

目前只为 Side-by-side 和 Three-up 绘制白色分割线。

Reference Focus 与 Analysis Grid 依赖画面内容本身来区分 Panel；当视频边缘为黑色时，边界可能不明显。

建议 Renderer/QML 暴露 `panelDividerRects`，所有多 Panel 模式都使用相同的 1～2 px 分隔线。

## 4. 沉浸模式会隐藏全部 A/B/C 身份

纯画布状态下 Source 标签完全隐藏。对于单视频没问题，但双/三路比较时，用户可能忘记左右分别对应哪个 Source。

建议沉浸模式保留可选的紧凑 Badge：

```text
左上：A
右上：B
```

只保留字母，不显示文件名，也可以在 1.5 秒后降低透明度。

---

# 六、Inspector 当前还不像完成态

Tabbed Inspector 已经有正确的四类结构，但内容完成度不平衡：

| Tab       | 当前完成度                 |
| --------- | --------------------- |
| Compare   | 只有说明文字和 Reset zoom    |
| Alignment | 功能较完整                 |
| Review    | 只有 In、Out 和 Export    |
| Info      | 只有文件名和 Renderer Ready |

## 1. Compare Tab 与 Viewer 控件职责冲突

Compare 页写着“高级参数仍在 Viewer”，因此本身几乎为空；而 Viewer 顶部又因为控件太多发生遮挡。

建议把以下内容移入 Compare Tab：

* Pair；
* Reference；
* Metric；
* Gain；
* Filter；
* Threshold；
* Exactness；
* Wipe Position；
* Reset View。

Viewer 只保留 Side/Wipe/Diff 和少量状态徽标。

## 2. Alignment 页缺少滚动容器

`AlignmentInspector` 本身是一个固定 Rectangle + Column，没有 Flickable。三路 Source、自动分析按钮和长 Compatibility 文本在 960×640 或长文本语言下可能被裁切或重叠。

应将整个内容放入 `Flickable` 或 `ScrollView`，Compatibility 信息作为 Column 最后一项，而不是单独锚定到底部。

## 3. Review Tab 缺少实际操作

应该至少增加：

* Set In；
* Set Out；
* Clear Range；
* Loop Range；
* Export Bad Case；
* Marker 列表。

当前用户只能通过键盘设置 In/Out，Inspector 只是显示结果。

## 4. Info Tab 信息不足

建议显示每个 Source 的：

```text
Resolution
FPS / timing mode
Frame count
Codec
Pixel format
Bit depth
Color matrix/range
Decode backend
Canonical / Reference role
```

“D3D11 renderer ready”不需要作为 Info 页最主要的内容。

---

# 七、Timecode 和 Range 的技术语义仍需修正

## 1. 当前 Timecode 不是严格的媒体 Timecode

当前 Timecode 由：

```text
frameIndex ÷ rounded FPS
```

计算。

这对于以下素材不严格正确：

* 29.97 FPS；
* 59.94 FPS；
* VFR；
* 非零起始 PTS；
* Drop-frame Timecode。

对于一个强调 frame-exact 的工具，不应将这种字符串直接作为严格 Timecode。

建议后端直接暴露：

```cpp
currentMediaTime
frameStartMediaTime(frame)
rationalFrameRate
timingMode
```

显示策略：

```text
整数 CFR：
    HH:MM:SS:FF

30000/1001 或 60000/1001：
    支持 NDF / DF

VFR：
    HH:MM:SS.mmm + Frame N
```

## 2. In/Out 仍然只是 QML 临时属性

`inFrame`、`outFrame` 和 `rangePlaybackActive` 当前定义在 Main.qml 中，没有进入项目保存状态。

因此：

* 保存项目后不会恢复；
* 改变 Reference 后不会按 MediaTime 重映射；
* 添加/移除 Source 后可能指向错误时刻；
* Loop 开启后没有清晰的关闭入口。

领域层原本已经有 In/Out Mark 概念，建议正式接入 Workspace/Project，并在拓扑或 canonical source 改变时按 MediaTime 转换。

---

# 八、视觉风格仍然不完全统一

当前自定义组件如：

* `ReviewActionButton`；
* `ToolbarCombo`；
* `TransportButton`；

都有统一的深色风格。

但新组件中仍大量直接使用默认 Qt 控件：

* EmptyReviewView 的 Button；
* ActiveSourceStrip 的 ToolButton/Menu；
* CompareModeBar 的 Button/ComboBox；
* TabbedInspector 的 TabBar/Button；
* Analysis Chrome 的 CheckBox、SpinBox、Button。

这些控件的最终外观取决于运行机器的 Qt Quick Controls Style 和 Windows 主题。在浅色系统主题下，可能出现浅色按钮、原生灰色下拉框与深蓝界面混杂。

建议新增统一组件：

```text
ReviewButton
ReviewToolButton
ReviewMenu
ReviewMenuItem
ReviewTabBar
ReviewCheckBox
ReviewSpinBox
```

或者在应用启动前显式固定 Controls Style，再统一设置 Palette。

快捷键帮助目前使用固定空格排版的多行字符串；翻译后无法保持对齐。应改成两列 `GridLayout`。

---

# 九、当前测试覆盖的不足

现有测试已经覆盖：

* Main QML 实例化；
* 默认 Viewport 占比；
* 960×640 布局；
* 1/2/3 Source 模式；
* 隐藏 Chrome 后的焦点和快捷键；
* Shortcut Preset；
* Source Panel 几何；
* Wipe 与 Drop Dialog 的 DPI。

但当前没有独立测试覆盖：

* 空状态 OSC 是否隐藏；
* PlayerOsc 自动显示/延迟隐藏；
* OSC 内部组件是否重叠；
* Frame Error Banner 是否在 Viewport 内；
* Source Label 与 Analysis Chrome 是否重叠；
* Timeline hover 的 Timecode/Thumbnail；
* Wipe 5%/95% 时标签重叠；
* 单视频 Inspector；
* Timeline Zoom 下 Range 裁剪；
* Basic/Windows Style 下的整窗截图。

UI CMake 中目前只为 ToolbarCombo、Drop Dialog、Wipe Handle 和 ReviewActions 注册了独立 QML 测试。

建议增加：

```text
tst_empty_review_view.qml
tst_player_osc.qml
tst_timeline_tracks.qml
tst_comparison_viewport_overlays.qml
tst_tabbed_inspector.qml
```

以及以下截图矩阵：

```text
窗口：960×640 / 1440×900
缩放：100% / 125% / 150%
模式：Empty / Single / Side / Wipe / Diff / Three-up
Inspector：Closed / Open
Style：Basic / Windows
```

该 SHA 当前也没有可见的 GitHub workflow run 或 commit status，因此以上判断是代码级界面审核，不等同于真实 Windows 机器上的像素验收。

---

# 十、建议整改优先级

## 第一阶段：必须先修的显示问题

1. 空状态隐藏 PlayerOsc；
2. 修复 OSC 自动隐藏和隐形交互；
3. 重新布局 OSC，消除 Timeline/Transport 重叠；
4. 修复 Frame Error Banner 锚点；
5. 建立 Viewer Overlay z 层级；
6. 修复 Timeline hover 预览同步；
7. 单视频允许打开 Review/Info Inspector；
8. Wipe 极端位置隐藏或缩减 Source 标签。

## 第二阶段：完成专业界面收口

1. 将 Diff 高级控件迁入 Compare Inspector；
2. Alignment Inspector 增加滚动；
3. Review Tab 增加 In/Out/Loop/Clear；
4. Info Tab 增加真实媒体信息；
5. Pair 只在相关模式显示；
6. Advanced Mode 显示当前选择；
7. 沉浸模式增加可选 A/B/C 紧凑标识；
8. 统一所有 Qt Controls 视觉组件。

## 第三阶段：完善审片语义

1. 后端 MediaTime Timecode；
2. In/Out 项目持久化；
3. Range 在 Reference 改变时按时间重映射；
4. Marker Hover 详细信息；
5. 缩略图只抓取视频 Surface；
6. OSC Pinned/Auto/Hidden 变为可保存的用户偏好。

---

# 最终评价

当前提交已经成功完成了**界面产品方向的转变**：

```text
之前：
工程面板包围视频

现在：
视频 Viewer 为主体，
专业能力通过上下文条、OSC、右键菜单和 Inspector 展开
```

这是正确且有价值的修改。

当前界面最需要处理的已经不是“大方向”，而是若干真实的布局和状态收口问题，尤其是：

> **空状态 OSC、OSC 内部重叠、顶部 Overlay 冲突、Frame Error Banner 越界，以及 Timeline Preview 不同步。**

修复这些问题之后，VCStation 的界面就可以从“功能完整的新 UI”进入“可以稳定交付给普通使用者的成熟 UI”。
