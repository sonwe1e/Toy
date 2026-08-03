# 核心结论

我审核的当前 `dev` HEAD 是 `ff665843`，项目版本为 **1.4.3**。你提出的三个问题都成立，其中前两个 UI 问题实际上有共同根因：项目虽然自定义了菜单背景和部分菜单项，却没有锁定 Qt 的 Popup 渲染方式，也没有给嵌套菜单统一指定自定义 delegate；Source Strip 又单独使用了一套原生 `Menu/MenuItem`。这会导致二级菜单透明、悬停样式丢失、弹窗位置异常，以及 `R`、`…` 点击后像是没有反应。

关于“倍数”，下面按**播放倍速**设计。当前项目并不存在隐藏的倍速能力：Application 层只有 Play/Pause，没有播放速率命令和 Session 状态，因此不能只增加一个 `1×/2×` 下拉框，否则 UI 会变化，实际播放调度仍是 1×。

版本上建议拆分：

* **1.4.4：纯 Bugfix**，修复菜单、Source 操作、快捷键冲突、资源释放竞态和发布门禁。
* **1.5.0：只增加播放倍速**，不同时加入其他功能。

必须合并为一个版本时，该版本从语义上应叫 **1.5.0**，因为倍速会改变 PlaybackCoordinator 的调度协议，而不只是 UI 修改。

---

# 一、Compare/View 二级菜单为什么会透明

## 1. 当前只自定义了菜单背景，没有自定义嵌套菜单 delegate

`VcsMenu.qml` 当前只设置了：

```qml
background: Rectangle { ... }
width: 280
padding: 4
```

但没有设置：

```qml
delegate
popupType
```

ApplicationMenuBar 中的 `Layout`、`Pair`、`Reference` 和 `Shortcut preset` 都是嵌套的 `VcsMenu`。叶子节点显式使用了 `VcsMenuItem`，但“Layout”这类代表子菜单的父菜单项是 Qt 隐式创建的，因此会退回当前 Controls Style 的默认 MenuItem。它不会使用 `VcsMenuItem` 的蓝色 Hover 背景、文字颜色和箭头布局。

## 2. 菜单渲染方式由 Qt Style 决定

Qt 6.8 之后，Menu 可以使用 `Popup.Item`、`Popup.Window` 或 `Popup.Native`。默认方式由 Style 和平台决定；使用 Native Menu 时，自定义 QML background 和 delegate 不参与最终绘制。Qt 官方明确建议：对菜单进行自定义后，应显式选择 `Popup.Window` 或 `Popup.Item`。子菜单还会继承根菜单的 Popup 类型。([Qt Documentation][1])

这正好解释了为什么一级菜单大致正常，而鼠标进入二级菜单区域后出现透明或样式突变。

## 修复方案

统一修改 `VcsMenu.qml`：

```qml
Menu {
    id: control

    popupType: Popup.Window
    cascade: true
    focus: true

    delegate: VcsMenuItem {
        popupInputContext: true
    }

    background: Rectangle {
        radius: control.menuRadius
        color: control.menuBackgroundColor
        border.width: 1
        border.color: control.menuBorderColor
        opacity: 1.0
    }
}
```

并在应用初始化阶段增加防御：

```cpp
QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuWindows);
```

两者不必都依赖，但显式 `popupType` 必须保留，不能继续让 Style 决定。

## 额外菜单缺陷

两路视频时，Compare → Layout 仍然存在。`Three up` 和 `Reference focus` 被隐藏，但 `Analysis grid` 只是 Disabled，没有隐藏，结果是一个几乎空的二级菜单。

应将整个 Layout Menu 限制为：

```qml
visible: control.sourceCount === 3
enabled: visible
```

而不是逐项制造空菜单。

---

# 二、`R` 和 `…` 点击无效的原因

你看到的 `R` 和 `…` 不属于 Viewer 中的 A/B 标签，而是 `ActiveSourceStrip` 中的视频管理按钮。

当前实现是：

```qml
text:
    sourceId === canonicalSourceIndex ? "R" : "⋯"

onClicked:
    sourceMenu.open()
```

这里有三个问题。

## 1. `R` 同时承担状态和按钮两种语义

`R` 表示当前 Reference，但它又是一个 ToolButton。用户自然会认为点击它会执行某项 Reference 操作。

打开菜单后，“Use as reference”因为当前已经是 Reference 而处于 Disabled 状态，只剩下 Remove Source。即使菜单正常显示，这也是一个语义不清的交互。

## 2. Source 菜单没有使用统一的 VcsMenu

Source Strip 使用的是原生：

```qml
Menu
MenuItem
```

而不是：

```qml
VcsMenu
VcsMenuItem
```

因此它会重复 Compare/View 二级菜单的 Popup 和透明问题。

## 3. `open()` 没有明确指定弹出位置

菜单属于 Repeater delegate 内部，仅调用 `sourceMenu.open()`，没有明确以 `…` 按钮为锚点设置弹出位置。在不同 Popup 类型和 DPI 下，它可能出现在错误位置、被遮挡，或者让用户感觉点击没有生效。

## 推荐的新结构

每个 Source Chip 改成：

```text
[A · video_a.mp4]  [R]  […]
[B · video_b.mp4]       […]
```

其中：

* `R` 是不可点击的状态 Badge；
* `…` 是所有多视频 Source 都拥有的唯一菜单按钮；
* Reference Source 的菜单只有 `Remove video`；
* 非 Reference Source 的菜单包含 `Make reference` 和 `Remove video`；
* 全部使用 `VcsMenu/VcsMenuItem`；
* Popup 明确定位在 `…` 按钮下方；
* Source 正在执行 Reference/Remove 时显示 Spinner，并禁用重复操作。

结构示例：

```qml
Rectangle {
    id: referenceBadge
    visible: chip.sourceId === control.canonicalSourceIndex
    // 纯状态，不接收点击
}

ToolButton {
    id: overflowButton
    text: "⋯"
    enabled: !chip.pending

    onClicked: sourceMenu.popup(
        overflowButton,
        Qt.point(0, overflowButton.height)
    )
}

VcsMenu {
    id: sourceMenu
    popupType: Popup.Window

    VcsMenuItem {
        visible: chip.sourceId !== control.canonicalSourceIndex
        text: qsTr("Make reference")
        onTriggered: control.referenceRequested(chip.sourceId)
    }

    VcsMenuItem {
        text: qsTr("Remove video")
        onTriggered: control.removeRequested(chip.sourceId)
    }
}
```

`busy` 已经传入 ActiveSourceStrip，却没有真正参与按钮状态计算；这也应同步清理。

---

# 三、菜单打开时仍可能触发底层播放快捷键

当前 Main.qml 通过 activeFocusItem 向上查找：

```qml
popupInputContext === true
textEditingInputContext === true
```

来决定是否关闭全局播放快捷键。

但是：

* `VcsMenu` 没有 `popupInputContext`；
* `VcsMenuItem` 没有 `popupInputContext`；
* ActiveSourceStrip 的原生 MenuItem 也没有；
* 使用 `Popup.Window` 后，菜单焦点可能存在于独立 Popup Window 中，Main Window 的 `activeFocusItem` 无法可靠反映这一状态。

因此在菜单打开时，按 Space、A/D、Tab 等按键，仍可能触发视频播放、逐帧或隐藏 Chrome。

## 修复方案

ApplicationMenuBar 暴露统一状态：

```qml
readonly property bool anyMenuOpen:
    fileMenu.opened
    || compareMenu.opened
    || analyzeMenu.opened
    || viewMenu.opened
```

ActiveSourceStrip 和 ReviewContextMenu 也分别暴露：

```qml
readonly property bool anySourceMenuOpen
readonly property bool contextMenuOpen
```

Main 使用显式状态，而不是只看 activeFocusItem：

```qml
readonly property bool anyPopupOpen:
    menuBar.anyMenuOpen
    || sourceBar.anySourceMenuOpen
    || viewerContextMenu.opened

readonly property int inputContext:
    modalDialogOpen ? ModalDialog
    : anyPopupOpen ? Popup
    : focusIsTextEditing(activeFocusItem) ? TextEditing
    : Viewer
```

只有 `Viewer` 状态允许媒体快捷键。

---

# 四、播放倍速的完整技术路线

## 1. 建议支持的速率

第一版控制在：

```text
0.25×
0.5×
1.0×
1.5×
2.0×
```

使用有理数，而不是浮点数：

```cpp
struct PlaybackRate {
    std::uint32_t numerator;
    std::uint32_t denominator;
};
```

对应：

```text
1/4
1/2
1/1
3/2
2/1
```

这样 CFR 和 VFR 长时间播放不会因为浮点累积产生 Timeline 漂移。

## 2. Application 层

新增：

```cpp
struct SetPlaybackRateCommand {
    CommandContext context;
    PlaybackRate rate;
};
```

加入 `PlaybackCommand` variant。当前只有 Play/Pause，没有 Rate Command。

SessionSnapshot 增加：

```cpp
PlaybackRate playbackRate = {1, 1};
```

ReviewController 增加：

```cpp
Q_PROPERTY(double playbackRate READ playbackRate NOTIFY stateChanged)

Q_INVOKABLE bool setPlaybackRate(double rate);
```

## 3. PlaybackCoordinator 调度

播放 deadline 不能继续直接使用 MediaTime 差值，而应使用：

```text
wall_delta = media_delta / playback_rate
```

即：

```cpp
wallDeadline =
    wallAnchor
    + scaleDuration(
        mediaTime(frame) - mediaAnchor,
        rate.denominator,
        rate.numerator
      );
```

当前 Coordinator 已经使用 wall-clock deadline、presentation lead 和 catch-up tolerance，因此倍速必须进入该层。

播放中切换倍速时应：

1. 保留当前已呈现 Frame；
2. 取消旧 cadence deadline；
3. 增加 playback generation；
4. 以当前 Frame 的 MediaTime 和当前 Clock 重新建立 Anchor；
5. 按新速率调度下一帧；
6. 不跳回首帧，不重复 ACK 当前帧。

## 4. 1.5×/2× 的帧完整性规则

60 FPS 视频在 2× 下相当于 120 帧/秒，120 Hz 显示器尚有机会逐帧呈现。

120 FPS 视频在 2× 下相当于 240 帧/秒，普通 120 Hz 屏幕不可能显示所有帧。因此必须明确：

* `≤1×`：保证所有 canonical FrameSet 依次呈现；
* `>1×`：允许跳过完整 FrameSet，但绝不允许 A/B/C Source 分裂；
* Wipe/Diff 必须始终使用同一个 canonical FrameSet；
* UI 在 1.5×/2× 时显示 `Fast review` 提示。

不能偷偷降低 Source 同步正确性来换取倍速。

## 5. UI 位置

PlayerOsc 当前包含 Timecode、Frame、Timeline 和 TransportBar，适合在右上角加入一个紧凑的 Speed Selector。

建议：

```text
00:01:23:14  Frame 2514/7000                 [1×]
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                    播放控制
```

同时在 View Menu 中增加：

```text
View
└── Playback speed
    ├── 0.25×
    ├── 0.5×
    ├── 1×
    ├── 1.5×
    └── 2×
```

两处必须绑定同一个 Snapshot State，不能各自维护局部值。

关于另一个可能含义——画面缩放：现有滚轮已经支持图像 Zoom，应额外在 Inspector 显示 `100% / 200% / 400%` 和 `Reset 100%`，但不要与播放倍速共用同一控件。

---

# 五、全盘筛查发现的其他问题

## P0：需要阻断发布

### 1. MultiSourceFrameProvider 的完成语义存在竞态

1.4.3 的 Release Quality Test 曾在低资源环境下复现 `7/40` 失败。当前处理方式是将 Release Quality 改为 Interactive/Normal Priority，而不是消除竞态。Commit 描述明确指出：Terminal Event 已到达，但异步 Frame Budget 释放可能尚未完成。

对应测试在收到 RequestSucceeded 后立即要求：

```cpp
EXPECT_EQ(budget.reservedBytes(), 0U);
```

这说明目前存在两种可能：

* Terminal 的语义错误：它并不代表所有资源已经退休；
* Provider 的顺序错误：它在释放 worker-owned FrameSet 前先发送了 Terminal。

单纯提高进程优先级只会降低复现概率，不会修复行为。

#### 修复

推荐保证：

```text
释放 worker 局部 FrameSet
→ 释放 FrameBudget Reservation
→ 发布 RequestSucceeded Terminal
```

如果架构上允许 Terminal 后异步释放，则必须新增明确的 `ResourcesRetired` 事件，并修改所有调用者和测试，不能让一个“成功完成”事件具备模糊语义。

完成后恢复 4 CPU BelowNormal Stress Test，至少连续运行 100 次。

---

### 2. 二级菜单没有任何专项测试

当前 UI Tests 包含 ToolbarCombo、Drop Confirmation、WipeHandle、ReviewActions、Empty View、PlayerOsc 和 Timeline，但没有：

```text
VcsMenu
ApplicationMenuBar
ActiveSourceStrip Menu
Nested submenu
```

这解释了为什么 Layout/Reference/Shortcut preset 的透明问题能够通过现有 CI。

必须新增：

```text
tst_vcs_menu.qml
tst_application_menu_bar.qml
tst_active_source_strip.qml
```

---

### 3. 当前最终 HEAD 的完整发布证据仍需重跑

1.4.3 Release Prep 之后，又连续修复了：

* Release Quality 资源配置；
* Resource policy allowlist；
* Previous MSI 路径经过 PowerShell 时被错误拆分；
* WiX ICE38/ICE43/ICE57。

因此 `c99eedcb` 的 1.4.3 Release Prep 不能视为最终验证版本。最终 Tag 必须指向或晚于 `ff665843`，并重新运行全部 Release Gates。本次连接器也没有返回该 HEAD 的 Combined Status，所以我不能证明精确 HEAD 已经全绿。

---

## P1：高优先级行为缺陷

### 4. 两秒 Catch-up Tolerance 可能掩盖真实卡顿

1.4.3 将播放追赶阈值从 500 ms 提高到 2000 ms，以避免低优先级测试环境中的 600～800 ms Stall 被计算成丢帧。

这对保留每一帧有合理性，但会让播放在极端情况下落后真实时间接近两秒，而 UI 仍处于 Playing。

建议区分：

```text
Exact review mode
    不跳帧
    延迟过大时进入 Buffering

Real-time mode
    超过阈值后跳过完整 FrameSet
```

播放倍速引入后，不能继续使用一个固定 2000 ms 阈值处理所有 Rate。

### 5. 菜单打开时快捷键可能穿透

前面已经确认，Input Context 只识别带 `popupInputContext` 的 activeFocusItem，而当前 VcsMenu/VcsMenuItem 没有该属性。

这应作为菜单修复的强制验收项。

### 6. Source Strip 状态和操作仍混在一起

`R` 是状态却做成按钮；`…` 是操作；两者占用同一位置。这是当前用户感受到“按钮点击无效”的根本交互问题之一。

### 7. 高层 DropArea 的层级过于激进

整个窗口存在一个 `z: 1000` 的全屏 DropArea。

虽然 DropArea 主要处理拖放事件，但当菜单改为 `Popup.Item` 或未来新增 Overlay 时，这种层级容易制造命中和遮挡问题。

建议：

* 菜单使用 `Popup.Window`；
* DropArea 自身放在正常内容层；
* 只有 `containsDrag` 时显示高层拖放 Overlay；
* 菜单打开期间拖入行为保持可预测。

### 8. Error Mapping 仍残留已删除领域

Main.qml 仍处理：

```text
clip-out-of-range
clip-not-found
export-record-not-found
duplicate-clip-selection
invalid-export-mode
invalid-export-geometry
source-fingerprint-mismatch
```

但 Clip、Export Record、Project Fingerprint 已从当前产品范围移除。

这些死分支不一定直接导致崩溃，但会让错误合同与当前领域模型脱节。

应新增自动测试：

```text
所有 MediaError stable ID
→ 必须存在一个用户文案
→ 不允许映射不存在的旧 ID
```

---

## P2：体验一致性问题

### 9. Source 菜单和主菜单使用两套组件

主菜单使用 VcsMenu，Source 使用原生 Menu，导致：

* Hover 风格不同；
* 字体和 Padding 不同；
* Popup 类型不同；
* 键盘行为不同；
* DPI 表现不同。

必须完全统一。

### 10. Source 操作缺少明确完成反馈

虽然当前 Session Facade 已经支持 bounded queue、generation 和 Source Identity，也暴露 Pending Source Index，但按钮只是显示 Spinner。

建议在操作期间显示：

```text
Changing reference…
Removing video…
```

失败时恢复按钮并显示具体 Toast，不能只有通用 `Review request failed`。

### 11. Compare/View 菜单宽度和 DPI 缺少回归矩阵

固定 280 px 菜单在中文翻译、150% DPI、960×640 窗口和右侧子菜单展开时需要专门验证。当前没有相关测试。

---

# 六、建议执行计划

## 阶段 A：先建立失败测试

代码修改前先增加以下红灯测试：

1. 打开 Compare → Layout，将鼠标从父项移动到子菜单，Popup 背景始终不透明。
2. 打开 View → Shortcut preset，Hover 状态不透明。
3. 打开 Reference/Pair 子菜单，文字、箭头和背景都使用 VcsMenuItem。
4. 菜单打开时 Space、A/D、Tab 不执行媒体操作。
5. 点击 Reference Source 的 `R` 当前行为被测试并确认不合理。
6. 点击任意 Source 的 `…`，菜单必须可见且位置正确。
7. Make Reference 和 Remove Video 真实提交对应 Intent。

---

## 阶段 B：统一 Popup 系统

修改：

```text
VcsMenu.qml
VcsMenuItem.qml
VcsMenuBar.qml
ApplicationMenuBar.qml
ReviewContextMenu.qml
ActiveSourceStrip.qml
```

完成：

* `popupType: Popup.Window`；
* 统一 `delegate: VcsMenuItem`；
* 统一 hover/focus/disabled/check 样式；
* 统一 Popup Input Context；
* Layout Menu 只在三路时存在；
* 删除所有裸 `Menu/MenuItem`；
* 键盘 Left/Right/Enter/Esc 正确；
* 关闭菜单后焦点返回 Viewer。

---

## 阶段 C：重构 Source Chip

将：

```text
R 或 …
```

改成：

```text
R 状态 Badge + 独立 … 菜单
```

同时：

* 所有操作使用 Source Identity，而不是把 SourceId 默认等同数组索引；
* Pending 状态按 Intent ID 和 Identity 绑定；
* 重复操作合并；
* 失败后完整回滚；
* Source Menu 关闭后才提交操作，避免 Popup 与 Session rebuild 同时销毁 delegate。

---

## 阶段 D：完成其余 Bugfix，发布 1.4.4

1. 修复 Provider Terminal/FrameBudget 释放竞态；
2. 菜单打开期间屏蔽全局快捷键；
3. 清除旧 Error Mapping；
4. 清理 DropArea 层级；
5. 补齐 Source 操作反馈；
6. 对最终 HEAD 重跑全部 CI、D3D11VA、Wipe/Diff Pixel、MSI Upgrade 和 Shutdown；
7. 不加入播放倍速。

---

## 阶段 E：只增加播放倍速，发布 1.5.0

涉及：

```text
Commands.h
SessionSnapshot
PlaybackCoordinator
ReviewController
ReviewPreferences/Session state
PlayerOsc
ApplicationMenuBar
ShortcutHelpOverlay
Performance tests
```

建议速率不持久化，打开新视频时恢复 1×，避免用户下次打开视频时意外以 2×播放。

---

# 七、验收标准

## 菜单

1. Compare、View、Pair、Reference、Layout、Shortcut preset 的二级菜单都不透明。
2. Basic 和 Windows Style 表现一致。
3. 100%、125%、150% DPI 均正常。
4. 二级菜单不会超出屏幕。
5. 鼠标从父菜单移入子菜单时不会闪烁或关闭。
6. 菜单打开时媒体快捷键不执行。

## Source 操作

7. `R` 只是状态，不再伪装成按钮。
8. 每个多路 Source 都有有效的 `…`。
9. Make Reference 能真实切换 Reference。
10. Remove Video 能真实删除对应 Source。
11. Pending、成功、失败都有可见反馈。
12. Side、Wipe、Diff、Three-up 下行为一致。

## 播放倍速

13. 0.25×、0.5×、1×、1.5×、2×真实改变 wall-clock 调度。
14. 播放中切换速率不跳帧位置。
15. Seek、Pause、Loop Range 后保持当前速率。
16. CFR 和 VFR 都按 MediaTime 调度。
17. 多路视频始终呈现完整 FrameSet。
18. 120 FPS × 2 的跳帧策略明确且可测试。

## 稳定性

19. Provider Terminal 到达后 FrameBudget 生命周期确定。
20. 4 CPU BelowNormal Stress Test 连续 100 次通过。
21. 最终 Release Tag 对应精确 HEAD。
22. Debug、Release、Quality、D3D11VA、Pixel Gate、Performance、MSI Upgrade 和 Shutdown 全部通过。

---

# 最终判断

目前最先要处理的不是倍速，而是**菜单系统统一和 Source 操作语义**。两个 UI 问题共享同一技术根因，修复时不能分别打补丁：

> **所有菜单必须使用同一个非 Native Popup、同一个 delegate、同一个 Input Context；所有 Source 必须将角色状态与操作按钮分离。**

在此基础上，倍速再作为唯一的新能力进入 PlaybackCoordinator。否则只增加一个 `2×` 按钮，会把目前已有的菜单、调度延迟和资源生命周期问题进一步放大。

[1]: https://doc.qt.io/qt-6/zh/qml-qtquick-controls-menu.html?utm_source=chatgpt.com "Menu QML Type | Qt Quick Controls | Qt 6.11.0"
