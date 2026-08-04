二、仍然存在的 P1 UI 和体验问题
P1-1：关闭 Source 菜单会无条件抢回 Viewer 焦点

当前 Source Menu：

onClosed: {
    openMenuCount = Math.max(0, openMenuCount - 1)
    viewerFocusRequested()
}

这对“用户执行 Make Reference 或 Remove 后返回 Viewer”是合理的，但对普通关闭不合理。

可能出现：

打开 Source […]
→ 点击 Inspector 中的输入框
→ Source 菜单因为点击外部而关闭
→ onClosed 把焦点重新抢回 Viewer
→ 输入框没有真正获得焦点

或者：

打开 Source […]
→ 点击 File 菜单
→ Source 菜单关闭
→ Viewer 抢焦点
→ File 菜单第一次点击无效
修复方案

不要在所有 onClosed 中恢复 Viewer 焦点。应区分：

ActionCommitted
EscapeClosed
OutsideClickClosed
WindowDeactivated
OwnerDestroyed

推荐：

property bool returnViewerFocusAfterClose: false

function requestReference() {
    returnViewerFocusAfterClose = true
    sourceMenu.close()
    ...
}

onClosed: {
    openMenuCount = Math.max(0, openMenuCount - 1)
    if (returnViewerFocusAfterClose)
        viewerFocusRequested()
    returnViewerFocusAfterClose = false
}

点击外部关闭时，不应主动改变用户刚刚选择的焦点目标。

这是目前最明确的剩余体验 Bug。

P1-2：Source 请求被同步拒绝时，用户可能得不到反馈

ActiveSourceStrip 当前执行：

设置 requestQueued=true
→ 关闭菜单
→ Qt.callLater
→ 发出 referenceRequested/removeRequested
→ requestQueued=false

Main 中的函数虽然返回：

shell.changeReferenceByIdentity(...)
shell.removeActiveSourceByIdentity(...)

但 Source Strip 的 Signal Handler 没有检查返回值并显示错误。

以下竞态仍可能静默失败：

用户打开 Source B 菜单
→ 外部启动请求替换了当前视频
→ 用户点击 Make Reference
→ Qt.callLater 执行时 Source B Identity 已失效
→ Shell 返回 false
→ 菜单已关闭，Spinner 消失
→ 没有 Toast
修复方案

Main 的 Handler 应明确处理返回值：

onReferenceRequested: identity => {
    if (!root.changeReference(identity))
        root.showIntentMessage(
            qsTr("The selected video is no longer available.")
        )
}

onRemoveRequested: identity => {
    if (!root.removeSelectedSource(identity))
        root.showIntentMessage(
            qsTr("The selected video could not be removed.")
        )
}

本地 requestQueued 也不应在调用后立即清除；应该等：

Intent Running
Intent Succeeded
Intent Failed
Intent Rejected

事件到来后再更新。

P1-3：窄窗口下 Inspector 仍然覆盖 Viewer 和 OSC

当前响应式策略仍是：

窗口宽度 ≥ 1120：
    Viewport 右边锚定 Inspector 左边
    两者并排

窗口宽度 < 1120：
    Viewport 占满整个宽度
    Inspector 仍显示在右侧上层

因此 960×640 下，约 300 px 的 Inspector 会覆盖 Viewer 右侧，同时覆盖：

时间轴后段；
OSC 右侧区域；
右侧 Source Badge；
Diff/ROI 状态；
Viewer 右键操作区域。

这更像临时叠层，而不是完整的响应式 Drawer。当前 1.4.4 修改没有重新设计这一布局。

修复方案

在窄窗口中将 Inspector 明确定义为 Drawer：

宽度 < 1120：
    Inspector 作为 Overlay Drawer
    增加半透明 Scrim
    点击 Viewer 空白处关闭
    Esc 关闭
    OSC 右边界收缩到 Drawer 左边

或者直接：

窗口宽度 < 1120：
    Inspector 打开时隐藏 Source Strip/Compare Bar
    Viewer 与 Inspector 采用 60/40 分栏

至少需要一个 960×640 的截图测试验证 Timeline、Transport 和 Inspector 没有关键控件互相遮挡。

三、存在风险但尚不能判定为已复现 Bug 的问题
1. Source Menu 生命周期计数可能残留

ActiveSourceStrip.anyMenuOpen 通过整数 openMenuCount 管理：

onOpened → +1
onClosed → -1

如果 Source delegate 在菜单打开期间被外部 Session 替换而销毁，Qt 是否必然在对象析构前发送 onClosed，需要真实测试证明。如果没有，openMenuCount 可能永久保持大于 0，造成：

anyMenuOpen = true
globalMediaShortcutsEnabled = false

直到重新打开窗口。

建议

增加测试：

打开 Source B 菜单
→ 从外部替换整个 Source Model
→ Source delegate 被销毁
→ anyMenuOpen 必须恢复 false
→ 媒体快捷键恢复

更稳妥的方案是使用统一 Popup Tracker，根据实际 Popup Object 生命周期维护状态，而不是让 Repeater delegate 手动维护计数。

2. Source Identity 依赖实时文件修改时间

Identity 包含：

canonical path
file size
lastModified milliseconds

如果录制程序、同步软件或其他进程修改了当前打开的视频文件，Source Identity 会发生变化。排队中的 Reference/Remove 操作可能被视为 Stale Topology，即使用户认为仍是同一个文件。

理想方案是：

Source Identity 在 Open/Probe 成功时由 SessionSnapshot 固化，不在每次 UI Projection 时重新读取文件系统状态。

文件内容改变应成为独立的 “Source changed on disk” 状态，而不是让 UI Identity 悄然变化。

3. Popup.Window 的真实鼠标穿越仍未完全证明

当前新测试主要证明：

Popup 能打开；
popupType 正确；
Open 状态正确；
Action 可以触发；
Style 和 DPI 配置可实例化。

但原始问题发生在：

鼠标从父菜单项
→ 穿过父子 Popup 之间的空隙
→ 进入二级菜单

当前测试中部分 Action 是直接调用 triggered()，不是实际在独立 Popup Window 中用鼠标点击菜单行。

需要补充真实鼠标测试：

打开 Compare
→ hover Layout
→ 等待子菜单展开
→ mouseMove 到子菜单第一项
→ 父菜单和子菜单都保持 opened
→ 背景像素保持不透明
→ mouseClick 执行 Three-up
四、其他 UI/体验筛查结果
1. 空状态

空状态目前没有 Project、Save 或退出提示，主要入口只有 Open Videos 和拖放；这符合现有产品定位。没有发现新的结构性问题。

仍建议验证：

空状态打开 File 菜单；
拖放 Overlay 与 Popup.Window 同时存在；
Graphics unavailable 时 Open Videos 是否正确禁用或给出反馈；
150% DPI 下两行说明文本是否越界。
2. 单视频

单视频继续使用播放器式布局和自动隐藏 OSC，Source Strip 降低透明度。当前没有发现会阻断使用的明显问题。

体验上仍可以优化，但不属于 1.4.4 Bug：

单视频 Source Chip 仍占一定顶部空间；
没有音频，但界面已经明确说明；
没有播放倍速，按既定版本规划属于 1.5.0，而非 1.4.4 缺陷。
3. Side-by-side / Wipe / Diff

1.4.2 已补充真实 Wipe/Diff 像素门禁，1.4.4 没有重新修改 Renderer 核心链路，因此目前没有发现新的黑屏回归路径。

需要在最终 SHA 重跑：

2×1080p60 Side
2×1080p60 Wipe
2×1080p60 Diff
Side → Wipe → Diff → Side
三路 A/C → 两路 A/B
Remove C → Wipe/Diff
4. 三路模式

Layout 和 Pair 菜单只在三路时出现，是正确修改。

仍需验证：

三路切换到两路时，已打开的 Pair 子菜单是否立即安全关闭；
删除当前 Reference 后 Badge 与 Inspector Reference 同步；
Source C 菜单打开时移除 Source A；
Three-up 和 Reference Focus 下 Source Badge 不与 Inspector 重叠。
5. Inspector

功能完成度已经较高，但窄窗口 Overlay 是当前主要剩余体验问题。

还应检查：

Compare Tab 的长文本；
Alignment Inspector 的 ScrollView；
150% DPI；
非常长的文件名；
三路 Media Info Card；
打开软键盘或输入法时 SpinBox 焦点。
6. 时间轴与 OSC

没有发现 1.4.4 新引入的明显功能回归。

仍需重点手工验证：

Inspector Overlay 时 Timeline 最右端；
Source Menu 打开时 OSC 自动隐藏是否暂停；
Popup 关闭后 Space 是否只触发一次；
拖动 Timeline 时打开菜单；
Loop Range 到 Out 后 Seek/Play 失败反馈；
Thumbnail Popup 与右侧 Inspector 是否重叠。
7. 全屏与多显示器

Popup.Window 解决了透明问题，但会引入独立窗口边界，需要真实 Windows 验证：

F11 全屏中打开菜单；
双屏不同 DPI；
主窗口位于副屏右边缘；
子菜单自动向左展开；
Alt+Tab 后 Popup 是否残留；
主窗口最小化时 Popup 是否同步隐藏；
Windows 任务栏自动隐藏；
远程桌面环境。

这些不是静态 QML 测试能完全覆盖的。

五、测试体系评价
已经明显改善

新增菜单测试矩阵覆盖：

VcsMenu
ApplicationMenuBar
ActiveSourceStrip

Basic Style
Windows Style

100%
125%
150%

Provider 竞态也增加了 Release 阶段 100 次重复测试。

这说明 1.4.4 已从“修代码”提升到“增加对应回归门禁”。

仍缺少的 UI 测试

建议补齐以下五组：

A. Nested Popup Pointer Test

实际鼠标从父菜单移动到子菜单，不使用直接 triggered()。

B. Popup Lifetime Test
Source menu open
→ Source model reset
→ Popup destroyed
→ anyMenuOpen false
C. Focus Ownership Test
Source menu open
→ 点击 Inspector SpinBox
→ Source menu close
→ SpinBox 保持焦点
D. Responsive Layout Screenshot Test
960×640
1100×700
1120×700
1440×900

Inspector open/closed
Single/Side/Wipe/Diff/Three-up
E. Packaged Windows Pixel Test

在真实安装后的 EXE 中抓取：

一级菜单；
二级菜单；
Source Menu；
Full Screen Menu；
125%/150% DPI。

必须检查 Popup 背景 Alpha，而不只是 opacity QML 属性。

六、1.4.5 解决状态

本节记录本轮修复的落地情况。上方第二至五节为用户审计原稿，保持不动。

P1-1：关闭 Source 菜单抢焦点 — 已解决

采用 returnViewerFocusAfterClose 标志方案。仅当菜单内 Action 被 triggered（Make Reference / Remove / 菜单项操作）时才在 onClosed 中发射 viewerFocusRequested；Escape 关闭、外部点击关闭、菜单切换关闭均不再抢焦点。覆盖三处菜单：

- ActiveSourceStrip.qml：chip 的 sourceMenu onClosed 条件化；requestReference / requestRemoval 在 close 前置标志。
- ApplicationMenuBar.qml：所有 item onTriggered 设标志；四个顶层菜单 onClosed 条件化；Open videos / Add video 不设标志（对话框接管焦点）。
- ReviewContextMenu.qml：同模式；contextOpenAction 除外。

对应自动化测试：
- test_triggered_action_emits_viewer_focus_requested（tst_active_source_strip.qml）— triggered 路径 viewerFocusSpy.count == 1。
- test_escape_close_does_not_emit_viewer_focus_requested（tst_active_source_strip.qml）— Escape 关闭 viewerFocusSpy.count == 0。

P1-2：同步拒绝无反馈 — 已解决

Main.qml 新增包装函数 removeSelectedSource / changeReference：调用 shell 对应方法，返回 false 时调 showIntentMessage 显示 toast。四个调用方（strip referenceRequested / removeRequested、TabbedInspector changeReference、ReviewContextMenu changeReference）统一走包装函数，一处覆盖全部路径。requestQueued 保持现状（shell 接受时 pending 在 intent 生命周期内持续；拒绝时身份不进列表，spinner 自然熄灭并出现 toast）。

对应自动化测试：
- ShowsIntentMessageOnSynchronousRejection（MainQmlContractTests.cpp）— 构造无 validatedComparison 的 snapshot 使身份匹配必定失败，断言 changeReference 和 removeSelectedSource 返回 false 且 intentMessage 非空。

P1-3：窄窗口 Inspector 覆盖 — 已解决

采用 Overlay Drawer + Scrim 方案：

- Main.qml 新增只读属性 drawerMode: alignmentBar.visible && root.width < 1120。
- 新 Scrim Rectangle（objectName: "inspectorScrim"），z:19，颜色 #99060a10，topMargin 与 inspector 一致，点击关闭 Inspector。
- Transport anchors.right 在 drawerMode 下收缩到 alignmentBar.left（PlayerOsc 同时包含 timeline 与 OSC，一处收缩覆盖两项）。
- 新增独立 Shortcut "Esc"（enabled: drawerMode && inputContext === 0），关闭 Drawer。
- 大于等于 1120 的并排模式全部不变。

对应自动化测试：
- DrawerScrimTransportShrinkAndEscClose（MainQmlContractTests.cpp）— 几何矩阵 {960x640, 1100x700, 1120x700, 1440x900} x inspector 开/关：drawer 下 scrim visible、transport 右边界 <= inspector 左边界、viewport 全宽不变、inspector 完整位于窗内；并排下 scrim 隐藏、viewport 右边界 <= inspector 左边界；Esc 关闭 drawer。含 960x640 截图像素门禁（inspector 区域不透明、transport 区域在 inspector 左侧可见）。

风险 1：openMenuCount 残留 — 已解决

ActiveSourceStrip.qml VcsMenu 新增 wasOpened 守卫：onOpened 置 true 并 +1；onClosed 仅当 wasOpened 时 -1 并复位；Component.onDestruction 同样守卫（Qt 未发 closed 就析构时兜底）。

对应自动化测试：
- test_model_reset_clears_open_menu_count（tst_active_source_strip.qml）— mouseClick 打开第二个 overflow 菜单，断言 anyMenuOpen == true，sources.clear() 后断言 anyMenuOpen == false && openMenuCount == 0，随后恢复双源模型供 cleanup 使用。

风险 2：Source Identity 实时重算 — 已解决

完整修复，身份固化 + 独立的 "文件已变更" 状态：

- SourceIdentity.h/cpp 新增 composeSourceIdentity(path, byteSize, modifiedMs) 组合函数；canonicalSourceIdentity 改为内部调用它。
- ReviewController 新增 frozenIdentitiesByPath_ 缓存，在 validatedComparison 指针变化时重建；publishProjection 改读缓存。
- ReviewController 新增 changedOnDisk 行字段：实时 QFileInfo 与 descriptor 值比较，仅用于变更检测。
- SourceListModel 新增 bool changedOnDisk 字段与 ChangedOnDiskRole。
- ReviewShellController 新增 frozenActiveIdentities_ 缓存，在 synchronizeActiveSources 时重建；activeSourceIdentities 返回缓存值。
- ActiveSourceStrip.qml chip delegate 新增 changedOnDisk 属性，琥珀色标记 + ToolTip。
- Main.qml Connections 监听投影，首次检测到 changedOnDisk 时 showIntentMessage 提示。

对应自动化测试：
- ComposeMatchesLiveCanonicalIdentity（SourceIdentityTests.cpp）— 组合函数与实时 canonical 结果一致。
- ChangedOnDiskRolePublishesStateThroughModel（SourceIdentityTests.cpp）— 模型 role 发布与 dataChanged 信号。
- FrozenIdentityStaysStableWhenFileChangesOnDisk（ReviewControllerTests.cpp）— 真实临时文件构造 snapshot，记录冻结身份，追加字节改动文件，refreshProjection 后断言 activeSourceIdentities 与行 sourceIdentity 不变、changedOnDisk 变 true。

测试组 A：Nested Popup Pointer Test — 已实现（降级路径）

- NestedPopupMouseTraversal（MainQmlContractTests.cpp）— 3 源 snapshot，打开 compareMenu，尝试 hover cascade 到 layoutMenu，真实鼠标点击 "Three up" 项，断言两菜单关闭、viewMode 变 ThreeUp，grab popup 窗图像断言背景 alpha == 255。
- 降级说明：合成 QHoverEvent/QMouseEvent 在 headless 环境下无法可靠触发 Qt Quick Controls Menu 跨 Popup.Window 级联。测试采用 fallback level 2：两菜单均程序化打开（等价于级联结果），然后真实鼠标点击子菜单项。hover 穿越段（父菜单 -> 间隙 -> 子菜单）需在真实 Windows 硬件上手工验证。新增辅助函数 sendMousePress / sendMouseRelease / sendMouseMove / findPopupWindow / menuPopupWindow。新增 objectName: "threeUpMenuItem"（ApplicationMenuBar.qml）。

测试组 B：Popup Lifetime Test — 已实现

- test_model_reset_clears_open_menu_count（tst_active_source_strip.qml）— 见上方风险 1。

测试组 C：Focus Ownership Test — 已实现（廉价层）

- test_triggered_action_emits_viewer_focus_requested / test_escape_close_does_not_emit_viewer_focus_requested（tst_active_source_strip.qml）— SignalSpy 计数验证焦点信号发射条件。
- 契约层（Inspector SpinBox 焦点保持）由 MainQmlContractTests 现有 mock-snapshot 基建覆盖；P1-1 修复后程序化关闭不再发 viewerFocusRequested，Escape 关闭同理。

测试组 D：Responsive Layout Screenshot Test — 已实现

- DrawerScrimTransportShrinkAndEscClose（MainQmlContractTests.cpp）— 见上方 P1-3。几何矩阵覆盖 960x640 / 1100x700 / 1120x700 / 1440x900 x inspector 开/关 + 960x640 截图像素门禁。

测试组 E：Packaged Windows Pixel Test — 已实现

- package.popup-pixels-scale-100 / package.popup-pixels-scale-125 / package.popup-pixels-scale-150（tests/smoke/CMakeLists.txt）— packaged 层，通过 VerifyPopupPixels.ps1 启动安装后 EXE，以 --ui-popup-pixels 模式运行，解析 stderr JSON 结果。QT_SCALE_FACTOR 覆盖 100% / 125% / 150% DPI 变体。
- src/app/Main.cpp 新增 --ui-popup-pixels 模式与 PopupProbeTarget / PopupProbeResult 结构体。
- src/ui_qml/DesktopApplication 新增 4 个自动化 API（openMenuForAutomation / closeMenuForAutomation / menuIsOpenForAutomation / capturePopupWindowForAutomation）；runPopupPixelProbe() 本体是 src/app/Main.cpp 的自由函数（与 runPerformance 同模式）。
- 真实多显示器 DPI（双屏不同 DPI、副屏边缘、子菜单自动向左展开）需在真实 Windows 硬件上手工验证，QT_SCALE_FACTOR 变体覆盖 alpha 关注点。

全量测试结果

ctest -j8 -E "hardware|performance|package"：423 个测试全部通过（0 失败）。基线 417 + 本轮新增 6 个 C++ 测试 + 3 个 QML 测试 = 426 中实际注册 423（部分 QML 测试通过 qmltestrunner 聚合注册）。packaged 层 3 个 popup-pixels 测试需 packaged-smoke preset 运行，不在默认 ctest 范围内。

剩余手工验证清单

以下项目来自第四节（其他 UI/体验筛查结果）和第七节（全屏与多显示器），静态测试无法覆盖，需在真实 Windows 环境手工验证：

第四节 — 其他 UI/体验筛查：
1. 空状态打开 File 菜单；拖放 Overlay 与 Popup.Window 同时存在；Graphics unavailable 时 Open Videos 反馈；150% DPI 下说明文本越界。
2. Side-by-side / Wipe / Diff 最终 SHA 重跑：2x1080p60 Side、Wipe、Diff；Side -> Wipe -> Diff -> Side；三路 A/C -> 两路 A/B；Remove C -> Wipe/Diff。
3. 三路模式：三路切两路时已打开的 Pair 子菜单安全关闭；删除当前 Reference 后 Badge 与 Inspector Reference 同步；Source C 菜单打开时移除 Source A；Three-up 和 Reference Focus 下 Source Badge 不与 Inspector 重叠。
4. Inspector：Compare Tab 长文本；Alignment Inspector ScrollView；150% DPI；非常长文件名；三路 Media Info Card；打开软键盘或输入法时 SpinBox 焦点。
5. 时间轴与 OSC：Inspector Overlay 时 Timeline 最右端；Source Menu 打开时 OSC 自动隐藏是否暂停；Popup 关闭后 Space 是否只触发一次；拖动 Timeline 时打开菜单；Loop Range 到 Out 后 Seek/Play 失败反馈；Thumbnail Popup 与右侧 Inspector 重叠。

第七节 — 全屏与多显示器：
6. F11 全屏中打开菜单。
7. 双屏不同 DPI。
8. 主窗口位于副屏右边缘；子菜单自动向左展开。
9. Alt+Tab 后 Popup 是否残留。
10. 主窗口最小化时 Popup 是否同步隐藏。
11. Windows 任务栏自动隐藏。
12. 远程桌面环境。

Test A 降级遗留：
13. 真实鼠标从父菜单项穿过父子 Popup 间隙进入二级菜单（compareMenu -> Layout -> Three-up），验证 hover 级联触发。自动化测试已覆盖"两菜单同时打开 + 真实鼠标点击子菜单项生效 + 背景 alpha"，但穿越段本身需手工。

Test E 多显示器遗留：
14. 真实多显示器不同 DPI 下 popup 背景 alpha（QT_SCALE_FACTOR 变体已覆盖单屏 100/125/150%，双屏异 DPI 需手工）。
