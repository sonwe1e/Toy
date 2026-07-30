# 核心结论

结合你的真实使用场景，这个项目下一阶段不应继续优先强化“超长视频、复杂工程配置、更多高级按钮”，而应转向：

> **面向 1～2 分钟、60/120 FPS 视频的高频拖入、逐帧审查和快速对比工具。**

当前界面仍以文件选择按钮为入口；顶部和对齐栏集中放置了大量控件；任意 `busy` 状态都会触发全屏 `Loading frame...` 遮罩。

新的优先级应调整为：

1. 拖入视频即可开始比较；
2. MSI 正确创建开始菜单入口；
3. 逐帧跳转不再闪烁全屏 Loading；
4. 默认界面只保留常用操作；
5. Offset、Auto Align、Find Drops、Save As 进入有解释的高级区域；
6. 增加清晰的对比分割线和拖动式 Wipe Compare；
7. 针对 60/120 FPS 短视频重新设计预取和性能门禁；
8. 移除用户包中不必要的 `ffprobe.exe`。

---

# 一、重新定义默认工作流

建议将默认使用流程简化为：

```text
拖入 2～3 个视频
        ↓
确认 Reference 和视频顺序
        ↓
自动打开 Strict Index 对比
        ↓
A / D 或左右键逐帧检查
        ↓
切换并排、拖动分割、差分或分析网格
```

高级对齐不应挡在默认流程中。只有检测到以下情况时才提示：

* 帧数不一致；
* 帧率不一致；
* 时长明显不一致；
* 中间可能存在掉帧；
* 用户主动进入高级对齐。

对于正常的 `GT + Prediction`，默认始终使用 Strict Index，避免自动对齐掩盖模型输出错误。

---

# 二、支持直接拖入视频

当前界面主要通过 `Select A`、`Select B` 和 `Add C` 选择文件，并没有形成拖放入口。

## 推荐交互

### 整体拖入

向窗口空白区域或视频画布拖入：

* 2 个视频：填入 A、B；
* 3 个视频：填入 A、B、C；
* 1 个 `.dvsproj`：打开项目；
* 1 个视频：在空会话中填入 A，不立即打开；
* 视频和项目混合拖入：拒绝并明确提示。

拖入期间显示：

```text
释放以比较 2 个视频
第一个视频将作为 Reference
```

释放后先显示一个轻量确认条：

```text
Reference: A ▼    A: gt.mp4    B: pred.mp4    [开始比较]
```

这样可以避免 Windows 拖放顺序与用户预期不一致。

### 拖到单独 Source 卡片

把一个视频拖到 A/B/C 卡片上时，仅替换该 source。

### 技术实现

在 `Main.qml` 增加根级 `DropArea`：

```qml
DropArea {
    anchors.fill: parent

    onEntered: dropOverlay.visible = true
    onExited: dropOverlay.visible = false

    onDropped: drop => {
        dropOverlay.visible = false
        controller.handleDroppedUrls(drop.urls)
    }
}
```

C++ 侧新增统一入口：

```cpp
DropResult ReviewController::handleDroppedUrls(const QList<QUrl>& urls);
```

不要在 QML 里只靠扩展名判断，C++ 仍需完成：

* 本地文件检查；
* Unicode 路径规范化；
* 重复路径检查；
* `.dvsproj` 与视频分类；
* 最多三路限制；
* 后缀不可信时继续交给 MediaProbe 判断。

---

# 三、开始菜单入口

这里应按 **Windows 开始菜单应用目录** 处理，而不是放入开机自启的 Startup 文件夹。视频比较工具不应默认随 Windows 自动启动。

当前 WiX 模板没有 `ProgramMenuFolder`、`Shortcut` 和 `RemoveFolder`，因此 MSI 安装后不会创建开始菜单快捷方式。

## MSI 应增加

```text
开始菜单
└── DualVideoStudio
    ├── DualVideoStudio
    └── Uninstall DualVideoStudio（可选）
```

同时建议注册：

```text
.dvsproj → DualVideoStudio
```

这样用户可以：

* 在开始菜单搜索程序；
* 双击项目文件打开；
* 右键 `.dvsproj` 选择“使用 DualVideoStudio 打开”。

## 验收

MSI 测试必须覆盖：

* 安装后快捷方式存在；
* 快捷方式目标和工作目录正确；
* 图标正确；
* `.dvsproj` 文件关联可用；
* 升级安装不会产生重复快捷方式；
* 卸载后开始菜单目录和关联被删除。

---

# 四、Compare 分割线

这里建议分两层实现。

## 第一层：并排视图增加明确分隔线

当前 Side-by-side 主要依赖画面边界，建议在视频之间显示 1～2 像素分割线：

```text
Reference │ Prediction
```

分割线应：

* 使用高对比但不刺眼的颜色；
* 在全屏和缩放模式中保持可见；
* 不进入截图或差分结果；
* 可在设置中关闭。

## 第二层：增加 Wipe Compare

这是更适合肉眼比较 VFI 结果的功能：

```text
Reference █████│░░░░░ Prediction
                ↑
             可拖动分割线
```

两路画面共享同一个：

* 缩放；
* 平移；
* ROI；
* aspect fitting；
* canonical frame。

用户拖动分割线即可观察边缘是否重影、纹理是否跳变。

三路模式下，Wipe 使用当前选择的比较边：

```text
A ↔ B
A ↔ C
B ↔ C
```

## 实现方案

增加：

```cpp
enum class SurfaceViewMode {
    SideBySide,
    ThreeUp,
    ReferenceFocus,
    Difference,
    AnalysisGrid,
    Wipe
};
```

`SurfaceRenderState` 增加：

```cpp
float wipePosition = 0.5F;
```

Renderer 分两次绘制：

1. Source 1 绘制完整 viewport；
2. Source 2 使用 scissor rect 绘制分割线右侧；
3. QML 在 `wipePosition` 上绘制手柄和 2 像素线。

第一版先实现并排分隔线，Wipe 作为紧随其后的交互增强。

---

# 五、重新组织“不明按钮”和高级功能

当前对齐栏在同一行放置：

* 多路 Offset；
* Auto align；
* Find drops；
* Confirm map；
* Undo map；
* Anchors；
* Apply；
* Strict reset。

这在较小窗口中容易显示不全，也不利于第一次使用者理解。

## 推荐信息架构

### 顶部主工具栏只保留

```text
打开/拖入
Reference
显示模式
比较对象
差分模式
播放控制
```

这些都是每次评测都会使用的功能。

### 右侧增加可折叠 Inspector

```text
┌ 高级对齐 ───────────────┐
│ 整体帧偏移              │
│ Source B  [-1] [0] [+1] │
│ Source C  [-1] [0] [+1] │
│                         │
│ [自动估计整体偏移]      │
│ [检测掉帧和重复帧]      │
│ [管理手动锚点]          │
│                         │
│ 当前模式：Strict Index  │
└─────────────────────────┘
```

默认折叠，仅在：

* 用户主动打开；
* 检测到兼容性问题；
* 自动分析完成等待确认；

时展开。

### 使用标准菜单承载低频操作

```text
文件
├── 打开视频…
├── 打开项目…
├── 保存项目
├── 另存项目为…
└── 退出

对比
├── 并排
├── 三路
├── 拖动分割
├── 差分
└── 分析网格

分析
├── 自动估计整体偏移
├── 检测掉帧和重复帧
└── 管理锚点
```

---

## 按钮重命名和说明

### Offset

不要只显示 `Offset`，改成：

```text
整体帧偏移
```

旁边增加帮助说明：

```text
+1 表示当前 canonical frame i
使用该视频的 frame i+1。
```

最好增加实时示例：

```text
Reference 100 → Prediction 101
```

### Auto align

改成：

```text
自动估计整体偏移
```

说明：

```text
用于两个视频整体相差几帧的情况。
不会检测视频中间的掉帧。
```

### Find drops

改成：

```text
检测掉帧和重复帧
```

说明：

```text
扫描完整视频，寻找中间缺失、
额外或重复的帧。
```

### Anchors

改成：

```text
手动指定对应帧
```

说明：

```text
用于自动对齐不准确时，
指定 Reference 和 Prediction 的对应位置。
```

### Save As

改成：

```text
另存评测项目为…
```

必须明确说明：

```text
保存的是 .dvsproj 项目文件，
包括视频路径、Reference、Offset、
锚点和视图设置，不会导出新视频。
```

当前 Project schema v3 确实保存 source、alignment 和 view state，而不是导出视频。

以后真正导出图片和错误案例时，应使用单独名称：

```text
导出当前对比图…
导出 Bad Case…
```

---

# 六、ffprobe 是否需要保留

## 结论

**对当前 GUI 和 CLI 的正常功能，外部 `ffprobe.exe` 没有必要。**

当前 `MediaProbe` 直接调用 FFmpeg 的 `libavcodec`、`libavformat` 等库完成媒体分析，并不是启动外部 `ffprobe.exe`。

但安装脚本仍把：

```text
ffmpeg.exe
ffprobe.exe
```

复制到用户安装目录。

## 推荐处理

### 用户 MSI 和便携 ZIP

移除：

```text
ffprobe.exe
```

CLI 的：

```text
DualVideoStudioCli --probe
```

继续使用内部 `MediaProbe`，不依赖外部进程。

### 开发者诊断包

确实需要人工排查媒体文件时，可以将 `ffprobe.exe` 放入单独的：

```text
Developer Tools artifact
```

不要让普通用户安装包携带没有运行时用途的工具。

同时审计 `ffmpeg.exe`。如果当前没有代理生成、转码或导出视频功能，它也可能没有必要存在于最终用户包中。FFmpeg 动态库和相应许可仍需保留。

### 需要调整的门禁

原来验证外部 `ffmpeg/ffprobe` 版本的 packaged test，应改为验证：

* 实际加载的 FFmpeg DLL 版本；
* CLI probe 能成功运行；
* 许可证和 provenance 存在；
* 安装目录没有未使用的外部工具。

---

# 七、Loading frame 不应每次全屏出现

## 结论

频繁逐帧时，**没有必要每次显示全屏 Loading frame**。

当前逻辑只要 `busy == true`，就会令 `overlayVisible == true`，并在已经显示帧时使用 `Loading frame...` 覆盖画面。

这会造成：

* 画面闪烁；
* 用户失去上一帧视觉参照；
* 连续按 A/D 时不断出现遮罩；
* 高速操作看起来比实际解码更慢。

## 重新划分状态

不要使用一个统一 `busy` 控制所有 UI。

```cpp
enum class UiOperationKind {
    Idle,
    OpeningComparison,
    FramePending,
    RunningAnalysis,
    SavingProject,
    LoadingProject
};
```

### OpeningComparison

还没有任何可显示帧时：

```text
允许使用全屏 Loading。
```

### FramePending

已经存在上一帧时：

```text
保留上一帧
不显示全屏遮罩
右上角显示轻量状态
```

例如：

```text
正在加载第 126 帧  ◌
```

### AnalysisRunning

在右侧 Inspector 显示进度，不遮挡视频：

```text
检测掉帧：42%
```

### SavingProject

只在状态栏显示：

```text
正在保存项目…
```

---

## 加载提示采用延迟显示

推荐规则：

| 等待时间         | UI            |
| ------------ | ------------- |
| 小于约 150 ms   | 完全不显示         |
| 约 150～800 ms | 右上角小型 spinner |
| 超过约 800 ms   | 非阻塞提示条，可取消    |
| 初次打开且无旧帧     | 全屏 Loading    |

这样 cache hit 和快速相邻帧不会产生闪烁。

## 连续跳帧行为

用户连续按十次下一帧时：

```text
当前显示：100
请求：101 → 102 → … → 110
最终只要求呈现：110
```

旧请求应被 supersede，最后一帧完成后直接显示 110。

导航按钮和快捷键不能因为 `FramePending` 被禁用。当前 QML 将 `!busy` 作为 Previous、Next 和 Timeline 的启用条件，需要拆除这一绑定。

---

# 八、针对短视频和 60/120 FPS 重新优化

以下按你说的 **60 FPS 为主、少量 120 FPS** 规划。

这类素材远低于当前 Find drops 默认的 50,000 帧上限，因此“超过 100,000 帧的分块对齐”不再是近期重点。当前已经具备 source cache、FrameSet cache 和方向感知预取。

## 1. 提高相邻帧预取深度

当前预取目标大致为：

```text
前进方向：+1、+2、+3、-1
后退方向：-1、-2、-3、+1
```

对于 120 FPS，三帧覆盖的时间非常短。建议改成基于帧预算的动态预取：

```text
60 FPS：
前方 6 帧，后方 2 帧

120 FPS：
前方 12 帧，后方 3 帧
```

但不能固定保留这些帧，应受 byte budget 限制：

```text
可用帧预算
÷ 每个 FrameSet 实际占用字节
= 可安全预取帧数
```

4K/P010 时自动减少，1080p/NV12 时可以增加。

## 2. 为 120 FPS 添加真实门禁

当前真实性能门禁主要是：

* 三路 1080p60；
* 三路 4K30 Main10。

应增加：

```text
2 × 1080p120，连续 60 秒
3 × 1080p120，连续 60 秒
```

测量：

* presentation ACK 连续性；
* source split；
* 完整 FrameSet drop；
* render latency；
* frame budget；
* worker thread 稳定性；
* 相邻逐帧 P50/P95；
* 连续按键 supersede latency。

120 FPS 下单帧周期更短，当前 ReviewController 的 16 ms UI 轮询也不再合理。它只能约 60 Hz 更新 UI 状态，应按照前面方案改成 Snapshot 事件驱动。当前 controller 确实使用 16 ms QTimer 轮询。

## 3. 增加时间步进

除了 ±1 帧，再增加：

```text
Shift + Left/Right：±5 帧
Ctrl + Left/Right：±1 秒
```

“±1 秒”应根据 canonical FPS 自动转换，不固定为 60 帧。

对于 120 FPS 视频，单纯 ±10 帧的跨度仍然较小。

## 4. 自动对齐采用按需建议

打开短视频后：

* frame count/rate/time 一致：不运行分析；
* 只有整体长度差异：提示“是否自动估计偏移”；
* 中间疑似异常：提示“是否检测掉帧和重复帧”；
* 不自动静默应用 sequence map。

---

# 九、修订后的实施路线

## 阶段 A：1.0.x 使用体验修复

优先完成：

1. MSI 开始菜单快捷方式；
2. `.dvsproj` 文件关联；
3. 视频和项目拖放；
4. Full-screen Loading 只用于首次打开；
5. FramePending 使用延迟小型提示；
6. 连续逐帧不禁用导航；
7. 并排视图增加明确分割线；
8. 修复按钮显示不全；
9. 重命名 Save As、Offset、Auto Align、Find Drops；
10. 移除用户包中的 `ffprobe.exe`。

这是最应优先发布的一组改进。

---

## 阶段 B：1.1 审查工作流重构

完成：

1. 顶部工具栏精简；
2. 高级对齐右侧 Inspector；
3. 标准 File/Compare/Analyze 菜单；
4. Wipe Compare 可拖动分割线；
5. 工具提示和帮助说明；
6. 60/120 FPS 动态预取；
7. ±1 帧、±5 帧、±1 秒导航；
8. 1080p120 性能门禁；
9. Snapshot 事件驱动 UI。

---

## 阶段 C：稳定性加固

完成：

1. Signature cache 设置内存上限；
2. packaged-smoke 覆盖真实 MSI 安装；
3. shutdown-soak；
4. Authenticode；
5. Release workflow；
6. provider future 等待改为 completion mailbox；
7. Bad Case 图片和 JSON 证据导出。

由于你的素材绝大多数短于两分钟，以下任务应降级：

* 超过 100,000 帧的分块对齐；
* 大规模跨进程 signature disk cache；
* 更复杂的长视频代理系统；
* 为非常长视频设计的多级磁盘分页。

---

# 十、最终验收标准

新版达到以下标准才算真正解决这批问题：

1. 安装后可在开始菜单搜索到 DualVideoStudio。
2. 拖入两个视频后无需打开文件选择器即可开始比较。
3. 拖入三个视频后可确认 Reference 和顺序。
4. Side-by-side 中间始终有明确分割线。
5. Wipe 模式可拖动比较任意两路。
6. 960 像素宽窗口下不存在按钮文字被截断。
7. 默认界面看不到 Offset、Anchors 等高级控制，展开 Inspector 后才显示。
8. 每个高级功能都有一句用途说明和示例。
9. Save As 明确显示为“另存评测项目”，不会被误解为导出视频。
10. 用户包不再携带没有运行时用途的 `ffprobe.exe`。
11. 连续快速跳十帧时不出现全屏 Loading。
12. 跳帧期间保留上一帧，最终只呈现最新请求帧。
13. 60/120 FPS 素材的相邻逐帧能够稳定命中预取缓存。
14. 2～3 路 1080p120 门禁通过且无 source split。
15. MSI 安装、升级、卸载、快捷方式和文件关联都有自动测试。

---

# 最终优先级

这批需求中，最重要的不是增加新的分析算法，而是让已有能力更容易被理解和使用。

建议实际执行顺序为：

> **开始菜单入口 → 拖放视频 → 去除逐帧全屏 Loading → 精简工具栏并增加高级侧栏 → Compare 分割/Wipe → 移除 ffprobe → 120 FPS 预取和性能门禁。**

这条路线与目前“短视频、高帧率、频繁逐帧”的真实工作负载最匹配。
