# 核心结论

最新分支 HEAD 是 `77c1e17d`。这次提交已经修复了上一轮的大部分 Viewer、OSC、时间轴和 Inspector 显示问题，但你现在感受到的异常来自更上层的 **应用外壳设计**：退出提示、File/Compare 菜单、顶部 Command Bar，以及 Project/Review 持久化概念仍然保留。

下一步不应继续微调这些弹窗，而应进行一次明确的产品简化：

> **VCStation 只管理当前打开的 1～3 个视频，不再管理 Project，不再要求保存会话；顶部只保留系统菜单、视频条、比较模式条和 Viewer。**

对应结果应是：

- 删除 `VCStation / VideoCompareStation / frame-exact review` 标题栏；
- 删除旁边的 `Open videos…` 和重复的 Inspector 按钮；
- 删除 Open/Save/Save As Project；
- 删除 `.dvsproj`；
- 普通退出不再出现保存提示；
- File 和 Compare 使用统一的深色菜单样式；
- 打开视频只通过空页面、File 菜单和拖放完成。

---

# 一、最新提交的总体评价

## 已经完成得比较好的部分

`77c1e17d` 的 Viewer 层已经比较成熟：

- 空状态不再错误显示 OSC；
- 单视频使用自动隐藏播放控制器；
- OSC 调整为紧凑布局；
- Source/状态/Diff 覆盖层增加了明确层级；
- Timeline hover、缩略图和 In/Out 范围得到完善；
- 单视频可以打开 Inspector；
- Compare、Alignment、Review、Info 四类 Inspector 内容更完整；
- MediaTime、DF/NDF Timecode 和 In/Out 持久化进入后端；
- Basic Controls Style 被统一固定。

因此，**视频画布内部的问题已经从结构性缺陷下降为局部打磨问题**。

## 当前的主要矛盾

现在的界面已经是播放器式 Viewer，但顶部和持久化流程仍然是“工程项目管理器”：

```text
播放器式 Viewer
        +
Project 打开/保存/脏状态
        +
传统 MenuBar 弹出菜单
        +
额外 Command Bar
        =
交互模型不统一
```

这也是为什么单独使用时会感觉 File、Compare 和退出提示明显突兀。

---

# 二、退出提示 UI 的问题

## 1. 当前提示本身不符合整体视觉

退出提示仍然使用一个固定高度为 190 px 的默认 `Dialog`：

- 标题为 `Save changes to this review?`；
- 正文询问是否保存；
- 底部为 Save、Discard、Cancel；
- 没有和 Drop Confirmation 一样的自定义深色背景、Header、Footer、Overlay；
- `closePolicy` 设置为 `NoAutoClose`。

因此它与当前自定义的深色 Viewer、Source Chip、Compare Bar 和 Inspector 不属于同一视觉体系。不同 Qt Style、DPI 或语言长度下，按钮间距和正文布局也容易显得不自然。

但这里不建议重新设计这个提示，因为更根本的问题是：**这个提示不应该存在。**

## 2. 普通打开视频也被视为“未保存内容”

当前判断逻辑本质上是：

```qml
sourceCount > 0 && (!hasProject || projectDirty)
```

因此只要打开了视频、但没有保存为 Project，关闭窗口就会被视为存在未保存内容。

这与播放器的常规认知完全相反：

```text
打开视频
播放几帧
关闭程序
```

用户不会认为自己创建了一份需要保存的文档。

更严重的是，Workspace 还将 Seek、逐帧、首帧、末帧等普通播放操作纳入 Project 变化判定。也就是说，即使打开了一个已经保存的 Project，仅仅查看不同帧也可能使其变成 dirty。

## 3. 目标行为

删除 Project 后，退出流程应改成：

```text
普通播放 / 比较 / 调整 Wipe / 切换模式
        ↓
点击关闭
        ↓
直接退出
```

以下状态也不需要保存提示：

- 当前帧；
- Wipe 位置；
- Zoom/Pan；
- ROI；
- In/Out；
- Reference；
- Alignment；
- 当前 Compare 模式。

用户偏好，例如 OSC 模式、快捷键方案、默认 Diff Filter，可以自动保存到 Settings。

只有后台存在不能安全中止的原子文件写入时，才需要显示：

```text
Export is still running
[Continue export] [Exit anyway]
```

Alignment 分析、Probe 和 Decode 应直接取消，不应阻挡退出。

---

# 三、File 菜单的问题

当前 File 菜单同时包含：

- Open new review；
- Add source；
- Open review project；
- Close current review；
- Save review project；
- Save review project as；
- Export Bad Case。

这里存在三个问题。

## 1. 菜单表达了三种不同对象

菜单中混合了：

```text
视频
Review
Project
```

用户需要理解三套概念：

- Video 是输入；
- Review 是运行中的会话；
- Project 是保存后的会话。

但实际日常任务只是：

```text
打开一个或几个视频
→ 播放或比较
→ 必要时导出 Bad Case
```

Project 没有提供足够价值，却增加了退出提示、文件关联、Save 状态和恢复流程。

## 2. Open Videos 重复出现

当前打开视频至少可以通过：

- 空状态中央按钮；
- File 菜单；
- 顶部 Command Bar 的 `Open videos…`；
- 拖入；
- Explorer 右键对比。

顶部 Command Bar 的入口没有增加新的能力，只重复了 File 和空状态入口。

## 3. File 菜单目标结构

删除 Project 后，File 菜单应收敛为：

```text
File
├── Open videos…                 Ctrl+O
├── Add video…                   Ctrl+Shift+O
├── Close videos                 Ctrl+W
├── ─────────────────────────
├── Export Bad Case…             Ctrl+E
├── ─────────────────────────
└── Exit                         Alt+F4
```

状态规则：

- `Add video…` 仅在当前有 1～2 个视频时可用；
- `Close videos` 仅在已有视频时可用；
- `Export Bad Case` 仅在已显示帧时可用；
- `Exit` 直接退出，不触发保存提示。

用户可见文案中不再出现：

```text
Review Project
Save
Save As
Relink
.dvsproj
```

---

# 四、Compare 菜单的问题

当前 Compare 菜单包含 Side by side、Wipe、Difference、Analysis Grid 和 Inspector 等内容；而主界面上又存在 Compare Mode Bar，右键菜单和 Inspector 中也有同类入口。

这不是简单的“重复”，而是三处状态入口容易产生不同的视觉反馈：

```text
顶部 Compare 菜单
Compare Mode Bar
Viewer 右键菜单
```

用户需要反复确认它们是不是同一个状态。

## 推荐结构

Compare Mode Bar 继续承担高频操作：

```text
[Side] [Wipe] [Diff] [Pair] […] [Inspector]
```

顶部 Compare 菜单承担键盘可访问性和完整命令：

```text
Compare
├── Side by side                 checked
├── Wipe
├── Difference
├── ─────────────────────────
├── Layout
│   ├── Three up
│   ├── Reference focus
│   └── Analysis grid
├── Pair                         仅三路显示
│   ├── A / B
│   ├── A / C
│   └── B / C
├── Reference
│   ├── Video A
│   ├── Video B
│   └── Video C
├── ─────────────────────────
└── Inspector
```

不再在 Compare 菜单中展示 Diff Metric、Gain、Threshold 等细节；这些属于 Inspector。

---

# 五、File/Compare 弹出菜单的视觉问题

当前应用主体已经使用自定义深色控件，但 MenuBar/Menu 仍主要依赖 Qt 默认绘制。即使最新提交固定了 Basic Style，菜单项的高度、边距、Check 指示器、Submenu 箭头和 Popup 背景仍可能与 Viewer 中的自定义控件差异明显。最新提交主要修复 Viewer、OSC、Timeline 和 Inspector，并没有从根本上重建 Menu popup。

建议新增一套统一菜单组件：

```text
VcsMenuBar.qml
VcsMenu.qml
VcsMenuItem.qml
VcsMenuSeparator.qml
VcsMenuShortcut.qml
```

## 视觉规范

```text
Menu 背景       #171e2a
边框            #40516a，1 px
圆角            7～8 px
菜单宽度        240～280 px
菜单项高度      34～36 px
水平 Padding    12 px
Hover 背景      #285da9
Disabled 文本   #637086
Shortcut 文本   #93a2ba
```

每一行采用固定三列：

```text
[Check/Icon]  Label                  Shortcut / ›
```

## 交互规范

- 菜单不是模态 Dialog，不添加背景遮罩；
- 点击外部或 Esc 关闭；
- Left/Right 在 File、Compare、View 之间切换；
- Enter 执行当前项；
- 执行后焦点返回 Viewer；
- Nested Menu 最多保留一层；
- 960×640、125% 和 150% DPI 下不得超出屏幕；
- File 和 Compare 菜单宽度固定，避免每次打开宽度跳变。

---

# 六、删除顶部 Command Bar

当前 Command Bar 占用约 48 px，并包含：

- `VCSTATION`；
- `VideoCompareStation · frame-exact review`；
- `Open videos…`；
- `Advanced Inspector`；
- Graphics 状态。

你提出删除这一行是正确的。

## 各元素迁移位置

| 当前元素 | 处理 |
|---|---|
| VCSTATION | 删除；窗口标题已有品牌 |
| VideoCompareStation · frame-exact review | 删除 |
| Open videos… | 删除；保留 Empty View、File 菜单、拖入 |
| Advanced Inspector | Compare Mode Bar、Compare 菜单和右键菜单已有入口 |
| Graphics ready | 成功状态删除 |
| Graphics unavailable | 使用 Viewer 中央错误 Overlay |
| Relink source | 随 Project 概念删除 |

## 删除后的布局

```text
Application MenuBar
Active Source Strip       仅有视频时显示
Compare Mode Bar          仅两路以上显示
Viewer                    占满剩余空间
Player OSC                覆盖在 Viewer 底部
```

空状态：

```text
Application MenuBar
Viewer / Empty State
```

这会直接释放 48 px 垂直空间。

在 960×640 窗口中，释放的空间非常有价值；多视频模式的画布高度也会进一步接近 85%。

---

# 七、删除 Project 概念的技术影响

这不是只删除几个菜单项，而是一次跨层简化。

当前 Project 概念包含：

- `.dvsproj`；
- Project Schema；
- Source 路径和指纹；
- Reference、Alignment、View、ROI、In/Out 持久化；
- Project Repository；
- WorkspaceCoordinator；
- Save/Load/Relink；
- dirty guard；
- Explorer 文件关联；
- MSI 安装和升级测试。项目能力在 README 和运行流程中仍属于正式功能。

## 删除后的状态归属

| 状态 | 新归属 |
|---|---|
| 打开的 1～3 个视频 | 当前运行 Session |
| 当前帧 | 当前运行 Session |
| Reference | 当前运行 Session |
| Alignment | 当前运行 Session |
| In/Out | 当前运行 Session |
| Wipe/ROI/Zoom | 当前运行 Session |
| OSC 模式 | 自动保存到 Settings |
| 快捷键 Preset | 自动保存到 Settings |
| 默认 Diff Filter | 自动保存到 Settings |
| Bad Case | 用户主动导出到文件夹 |

Session 关闭后，运行状态自然消失，不需要 Save。

---

# 八、具体执行计划

## 阶段 A：先完成用户可见简化

### A1. 删除 Command Bar

修改 `Main.qml`：

- 删除 `commandBar`；
- Source Strip 改为锚定内容区顶部；
- Compare Bar 锚定 Source Strip；
- Viewer 和 Inspector 的 `topMargin` 删除 Command Bar 高度；
- 删除 Open Videos、Advanced Inspector 和 Graphics 状态按钮。

### A2. 简化 File 菜单

删除：

```text
Open review project
Save review project
Save review project as
Relink source
```

重命名：

```text
Open new review → Open videos
Close current review → Close videos
Add source → Add video
```

增加：

```text
Exit
```

### A3. 重做 File/Compare Popup 样式

新增统一菜单组件，并替换 Main 中的默认 Menu/MenuItem。

### A4. 删除正常退出提示

- 删除 `WorkspaceDialogs.qml` 中 unsaved changes Dialog；
- 删除 `onClosing` dirty guard；
- Window Close 直接执行 shutdown；
- 分析和解码任务在 shutdown 时取消。

此阶段结束后，用户已经看不到 Project，但底层代码可能暂时仍存在，不能在这一中间状态发布。

---

## 阶段 B：删除 Project 应用与领域模型

### B1. 删除 UI Controller 接口

从 `Main.qml` 和 `WorkspaceController` 删除：

```text
projectDirty
hasProject
canSaveProject
relinkRequired
restoredViewState
projectPath
save()
saveAs()
openProject()
relinkSource()
```

### B2. 删除 Application 层 Project 流程

删除或重构：

```text
WorkspaceCoordinator::openProject
WorkspaceCoordinator::saveProject
WorkspaceCoordinator::relinkSource
ProjectLoadRequest
ProjectSaveRequest
ProjectRelinkRequest
ProjectLoaded
ProjectSaved
SourceRelinkPrepared
```

`WorkspaceCoordinator` 如果只剩 Close Session 和 Marks，应重命名为：

```text
ReviewSessionController
```

或者直接把这些能力并入 `ReviewController`。

### B3. 删除 Domain 层

删除：

```text
src/domain/include/dvs/domain/Project.h
src/domain/src/Project.cpp
ProjectId
ProjectState
ProjectViewState
ProjectAlignmentState
ProjectRevision
```

当前 Session 仍继续使用：

```text
ValidatedComparisonSet
CanonicalTimeline
SessionSnapshot
Alignment maps
```

### B4. 删除 Persistence 层

删除：

```text
ProjectJson
ProjectRepository
Project Schema v2/v3/v4
Atomic Project Save
Derived Alignment Cache 对 Project 的依赖
```

保留：

```text
SettingsRepository
Bad Case Export
```

Alignment cache 若仍有运行时性能价值，应按 Source fingerprints 作为独立缓存，而不是依赖 Project。

---

## 阶段 C：删除启动、安装与文档残留

### C1. 启动参数

删除：

```text
StartupRequest::OpenProject
.dvsproj 命令行解析
拖入 project 文件
```

保留：

```text
Empty
PlaySingle
Compare
```

### C2. 安装包

从 WiX 中删除：

```text
.dvsproj 文件关联
VCStation.Project ProgID
对应 Registry 清理逻辑
```

升级安装时必须删除 1.1/1.2 旧版本留下的 `.dvsproj` 注册项。当前安装模板仍包含 Project 和 Shell 关联组件。

### C3. 文档

删除或重写：

```text
docs/project-schema-v3.md
docs/project-schema-v4.md
README 中 Project 章节
Release Notes 中 Project Schema 宣传
USERPLAN 中 Project 保存/恢复路线
```

已有 `.dvsproj` 文件留在用户磁盘上即可，不自动删除用户文件。

---

# 九、测试计划

## 1. 退出行为

```text
空状态退出
单视频退出
双视频退出
播放中退出
逐帧后退出
改变 Reference 后退出
改变 Wipe 后退出
设置 In/Out 后退出
Alignment 分析中退出
```

除不可取消的文件导出外，全部不得出现保存提示。

## 2. File Menu

验证：

- 项目相关项完全不存在；
- 960×640、1440×900；
- 100%、125%、150% DPI；
- 键盘打开与导航；
- Esc 和点击外部关闭；
- Open/Add/Close/Export 状态正确；
- 执行后焦点返回 Viewer。

## 3. Compare Menu

验证：

- 单视频时 Compare Disabled 或只显示无效状态；
- 两路只提供 Side/Wipe/Diff；
- 三路增加 Layout、Pair 和 Reference；
- Checked 状态与 Viewer 完全一致；
- 与 Compare Mode Bar 双向同步；
- 改 Reference 后等待后端重建成功再更新 Check。

## 4. 页面空间

验收标准：

```text
删除 Command Bar 后：
单视频默认画布高度占比 ≥ 88%
双/三路默认画布高度占比 ≥ 82%
```

## 5. 概念清理

代码和用户界面中搜索：

```text
project
dvsproj
review project
save review
relink
```

允许存在于旧版 Release Notes 或 Migration 注释中，活动产品代码与 UI 中不得残留。

---

# 十、建议的提交拆分

为了让每次修改容易审核，建议拆成五个独立提交：

```text
1. ui: remove command bar and simplify video shell
2. ui: restyle File and Compare menus
3. ui: remove project actions and unsaved exit guard
4. core: remove project persistence and workspace flows
5. packaging: remove dvsproj association and update tests
```

不要将“隐藏 Project 菜单”当成完成状态。最终提交必须同步删除后端、Installer、测试和文档，否则代码会留下大量永远不会再使用的分支。

---

# 最终判断

`77c1e17d` 已经把 **Viewer 内部** 做得比较完善，但 **Viewer 外部** 仍然继承了旧版 Project 工具的思维。

你提出的三个修改方向应全部采纳：

1. **退出提示不应美化，而应随 Project 删除。**
2. **File/Compare 菜单应重新设计为统一的深色、紧凑、非模态 Popup。**
3. **标题 Command Bar 应整体删除，不能只隐藏文字或按钮。**

最终产品应变成：

> 打开视频、比较视频、操作视频、导出结果；关闭时直接结束。没有 Project，没有 Save，没有 Relink，也没有“未保存 Review”。
