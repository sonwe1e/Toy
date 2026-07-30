# 总体结论

`feature/vcstation-userplan` 已经完成了大部分用户体验重构，方向正确，而且不是只改了界面文案：拖放、Wipe、对齐侧栏、逐帧 Loading 优化、事件驱动 UI、开始菜单入口、文件关联、Bad Case 导出和 120 FPS 门禁都进入了实际代码与测试。

但**当前还不能直接合并并发布**。我确认了 4 个合并前必须修复的问题：

1. VCStation 仍使用 `1.0.0`，无法可靠升级已有的 DualVideoStudio 1.0.0。
2. 新增的 `1080p120-2source` 性能门禁当前必然失败。
3. Source A，即 `SourceId=0` 的对齐错误归属 bug 仍然存在。
4. Release workflow 没有校验 tag 与程序版本，也没有覆盖 Quality 和硬件性能门禁。

综合判断：

| 维度      | 当前完成度 | 判断                   |
| ------- | ----: | -------------------- |
| 七项用户需求  | 约 90% | 基本完成                 |
| UI 交互质量 | 约 85% | 可进入试用                |
| 运行时架构   | 约 80% | 有实质改善，但部分“非阻塞化”未完全实现 |
| 安装升级    | 约 70% | 新装闭环，旧版升级未闭环         |
| 发布流水线   | 约 65% | 已有骨架，尚不能作为正式发布门禁     |
| 合并准备度   |     否 | 需要先处理 P0             |

---

# 一、七项需求的实际完成情况

## 1. 拖动视频进入界面：基本完成

根窗口已经增加全局 `DropArea`，支持拖入视频或 `.dvsproj`。C++ 会负责：

* 限制最多三项；
* 验证本地文件；
* 规范化 Unicode 路径；
* 检测重复文件；
* 禁止项目文件和视频混合拖入；
* 对两个或三个视频弹出顺序与 Reference 确认。

测试也覆盖了中文文件名、非标准扩展名、重复路径、缺失文件和项目混拖。

### 尚未完全实现的部分

当前只有**整个窗口级拖放**，没有每个 Source 卡片独立的 DropArea。因此还不能把一个视频直接拖到 B 卡片来替换 B。

另外，单独拖入一个视频时，当前逻辑倾向于重新设置 A，而不是：

```text
A 为空 → 填 A
A 已有、B 为空 → 填 B
A/B 已有、C 为空 → 填 C
拖到指定卡片 → 只替换该卡片
```

### 建议

下一步补充：

```text
SourceCard.qml
└── DropArea
    ├── dropTargetSourceId
    ├── hover highlight
    └── replace confirmation
```

这属于 P1，不阻挡第一轮内部试用。

---

## 2. 开始菜单入口：代码完成，但升级链路未完成

WiX 模板已经增加：

* `VCStation` 开始菜单目录；
* VCStation 快捷方式；
* 卸载时清理目录；
* `.dvsproj` 文件关联；
* 双击项目时执行 `VCStation.exe "%1"`。

真实 MSI 测试也已经验证：

* 安装；
* 开始菜单快捷方式；
* HKLM 文件关联；
* CLI startup/probe；
* 卸载清理。

应用本身也支持把单个 `.dvsproj` 命令行参数作为初始项目打开。

### P0：版本号导致无法正常升级

当前工程仍然是：

```cmake
project(VCStation VERSION 1.0.0)
```

资源版本也被硬编码为：

```text
FileVersion    1.0.0
ProductVersion 1.0.0
```

而 WiX 明确配置：

```xml
AllowSameVersionUpgrades="no"
```

已有公开版已经是 DualVideoStudio 1.0.0，因此新的 VCStation 1.0.0 不能形成可靠的 Major Upgrade。

### 必须修改

建议发布为：

```text
VCStation 1.1.0
```

并将版本号改为单一来源：

```text
CMake PROJECT_VERSION
        ↓
configure_file
        ↓
VCStation.rc
CPack
Release 名称
SHA256 文件
```

不要继续手工维护 `VCStation.rc` 中的版本。

### 升级测试也未真正启用

`VerifyMsiPackage.ps1` 支持传入 `PreviousMsiPath` 做升级测试，但 CTest 注册时没有提供旧 MSI，因此目前只测试了新装和卸载。

需要加入：

```text
DualVideoStudio-1.0.0-windows-x64.msi
        ↓
安装旧版
        ↓
安装 VCStation 1.1.0
        ↓
检查旧 ARP 条目、旧快捷方式和旧安装目录
        ↓
验证新快捷方式、关联和程序启动
```

---

## 3. Compare 分割线与 Wipe：完成质量较高

Side-by-side 和 Three-up 已增加明确分割线。

Wipe 不是只在 QML 画了一条线，底层 renderer 已真正支持：

* 任意 A/B、A/C、B/C；
* `wipePosition`；
* 共用 zoom、pan 和 ROI；
* 左右 UV 和 destination rect 分割；
* 缺少任一路时显示黑色。

WARP 像素测试验证了：

* 选择 A/C；
* Wipe 位置为 25%；
* 左右区域确实来自两个不同 source。

这一项可以判定为**完成**。

### 后续小优化

在 Wipe 模式下建议增加：

```text
A | C
```

的悬浮标签，并在空间重采样时显示：

```text
Resampled comparison
```

避免用户误认为不同尺寸视频仍是 pixel-exact。

---

## 4. 不明按钮、Offset、Auto、Find、Save As：主要问题已解决

当前已经采用较合理的信息架构：

* `ApplicationWindow` 标准菜单；
* File、Compare、Analyze 分类；
* 高级对齐放入右侧 Inspector；
* Inspector 默认隐藏；
* Offset、Auto Alignment、Find Drops 和 Anchors 有更完整说明；
* Save As 改成更明确的“另存评测项目”。

QML contract test也确认：

* 高级 Inspector 默认不可见；
* 最小窗口宽度为 960；
* 顶部比较控制区可横向滚动；
* 传输控制按钮有实际尺寸。

### 仍有两个体验问题

#### 语言混杂

当前源字符串中英文混用。例如大多数菜单是英文，而“另存评测项目”直接使用中文。

需要统一策略：

* 源字符串全部英文，再通过 `zh_CN.ts` 翻译；
* 或当前版本全部中文。

不建议继续在同一层级混用。

#### 横向滚动并不等于控件真正清晰

比较工具栏使用 `Flickable` 解决小窗口裁切，但没有明显的滚动条或溢出提示。用户可能不知道右侧还有控件。

建议增加：

```text
右侧渐变遮罩 + “›”
或
ScrollBar.horizontal
```

更理想的方案是让低频 Diff Metric、Gain、Filter 进入右侧 Inspector，顶部只保留：

```text
View
Compare pair
Reference
Advanced
```

---

## 5. ffprobe：用户安装包已经正确移除

安装脚本现在只部署 VCStation、Qt 和链接依赖 DLL，不再复制外部 `ffmpeg.exe` 或 `ffprobe.exe`。

打包门禁还明确禁止这两个 EXE 出现在最终包中，并使用内部 `VCStationCli --probe` 验证 libavformat/libavcodec 路径。

这一项可以判定为**完成**。

### 剩余清理

`tools/dependencies/ffmpeg-runtime.json` 仍然 pin 了包含 `ffmpeg.exe` 和 `ffprobe.exe` 的 Gyan archive。

它已经不参与用户包，应当：

* 删除该 manifest 和对应 bootstrap 路径；或
* 明确改名为 `ffmpeg-developer-tools.json`；
* 文档说明仅供开发诊断使用。

否则供应链文档会让人误以为发布仍依赖这两个外部工具。

---

## 6. 高频逐帧时反复 Loading：主体问题已经解决

这是本次实现中完成度较高的一部分。

现在已经拆分：

```text
busy         = 打开、保存、应用配置等状态改变
framePending = 正在请求新帧
```

上一帧已经存在时，`framePending` 不会显示全屏 Loading；仅在等待超过约 140 ms 后显示轻量提示。

导航在上一条请求仍未完成时继续可用，Controller 单独保存最新 navigation context。

测试也验证：

* navigation pending 不等于 busy；
* First/Previous/Next/Last 仍可继续提交；
* 旧 terminal 不会清除最新 pending；
* 只有最新 context 完成才结束 pending。

UI 投影也从 16 ms 常驻轮询改为 coordinator 发布 Snapshot 后，通过 queued invocation 通知 GUI。

这一项可以判定为**完成**。

---

## 7. 面向短视频、60/120 FPS 优化：部分完成

Prefetch 已根据 canonical FPS 调整：

```text
<50 FPS：3 前 / 1 后
50～99 FPS：6 前 / 2 后
≥100 FPS：12 前 / 3 后
```

调度器对 look-ahead 最大限制 12，look-behind 最大限制 3，并有单元测试。

硬件测试也新增：

* 两路 1080p120，60 秒；
* 三路 1080p120，60 秒。

### P0：两路 120 FPS 门禁必然失败

性能程序把全硬件解码条件写成：

```cpp
backends.size() == 3U
```

因此：

```text
1080p120-2source
```

即便两路全部为 D3D11VA，也会因为 backend 数量不是 3 而失败。

应该改成：

```cpp
const std::size_t expectedSourceCount =
    sources.third.has_value() ? 3U : 2U;

const bool allHardware =
    backends.size() == expectedSourceCount &&
    std::all_of(...);
```

并将 `expected_source_count` 写入 JSON report。

### “预算感知预取”只完成了一半

当前窗口只根据 FPS 决定，没有根据：

* 分辨率；
* NV12/P010；
* source 数量；
* 剩余 FrameBudget；
* 每帧实际 accounted bytes；

动态调整。

好的一点是底层 source cache 本身受 byte budget 约束，不会无限增长。但 4K P010 下仍可能提交 12+3 个无意义预取，再被 cache 或队列淘汰。

建议计算：

```text
available cache bytes
÷ estimated bytes per source frame
÷ source count
= safe prefetch depth
```

最后：

```text
actualLookAhead = min(fpsPreferredLookAhead, safeDepth)
```

---

# 二、新发现的 P0 正确性问题

## Source A 对齐错误仍会丢失 source attribution

`SourceId` 从 0 开始，A 就是 0。

但 `AlignmentAnalysisService` 仍使用：

```cpp
sourceId == 0 ? std::nullopt : optional{sourceId}
```

这会导致发生在 Source A 上的部分分析错误被错误显示为“整个 comparison 的错误”。

应改成：

```cpp
MediaError serviceError(
    std::string detail,
    std::optional<domain::SourceId> sourceId = std::nullopt);
```

明确调用：

```cpp
serviceError("global failure");
serviceError("source failure", source.id);
```

必须增加 Source 0、Source 1 和无 source 三种测试。

---

# 三、运行时架构评估

## 1. Event-driven UI 改造正确

Coordinator 增加 `statePublished` callback，Snapshot 和 command terminal 发布后通知 GUI。

这一改造比原来的 16 ms QTimer 轮询更合理，尤其适合：

* 120 FPS；
* 空闲功耗；
* 高频逐帧；
* 后台对齐进度；
* 状态变化的低延迟呈现。

这部分建议保留。

---

## 2. Provider “非阻塞化”还没有真正完成

SourceDecodeActor 已支持 callback completion，Provider 也不再直接持有 `future`。

但是 Provider worker 仍然执行：

```cpp
for (...) {
    completionMailbox->take();
}
```

并阻塞等待所有 source completion。

因此当前实质是：

```text
future.get()
→ condition-variable mailbox.take()
```

改善了接口和 completion identity，但 Provider 控制线程仍不能在等待期间立即执行下一项 open/close operation。

对于当前 2～3 路短视频场景，这不是发布阻断项；但文档不能将其描述为“provider event loop 完全非阻塞”。

真正完成形态仍应是：

```text
actor completion
    ↓
provider shared completion queue
    ↓
pendingSets[operationId]
    ↓
最后一个 slot 完成后 publish
```

Provider worker 在中间不等待某一个 request 的所有结果。

---

## 3. Signature cache 有边界了，但不是 LRU

当前默认最多保存 50,000 个 signature，使用插入顺序 FIFO 淘汰。

针对你的素材：

```text
2 分钟 × 120 FPS × 3 路 = 43,200 signatures
```

50,000 的默认值是合理的，能够覆盖主要工作负载。

目前仍存在：

* 不是 byte budget；
* cache hit 不刷新顺序；
* 一个新 source 可能淘汰另一个 source 的部分 range；
* `findRange()` 缺少任一帧就需要重新解码完整范围。

但结合短视频定位，这些可以放到 P2，不需要阻挡版本发布。

---

# 四、安装与发布流程仍需收口

## 1. Release workflow 缺少版本一致性校验

当前任何 `v*` tag 都会触发发布。

没有验证：

```text
tag v1.1.0
==
CMake PROJECT_VERSION 1.1.0
==
VCStation.rc 1.1.0
==
MSI ProductVersion 1.1.0
```

因此可能出现：

```text
GitHub Release v1.1.0
包文件 VCStation-1.0.0
EXE 版本 1.0.0
```

必须在 workflow 第一阶段加入 fail-closed 校验。

---

## 2. Release workflow 没有运行完整质量门禁

当前 Release workflow执行：

* Release build；
* Release CTest；
* EXE 签名；
* ZIP/MSI；
* packaged/soak；
* MSI 签名；
* draft release。

但没有执行：

* Debug tests；
* format-check；
* QML lint；
* clang-tidy；
* D3D11VA hardware tests；
* 1080p60、4K30、1080p120 performance；
* 确认硬件门禁结果属于同一个 commit SHA。

建议采用两阶段发布：

```text
Release Candidate Gate
├── Debug
├── Release
├── Quality
├── Hardware
├── Performance
└── packaged/soak

Publish
└── 只允许消费同一 SHA 的已通过 artifacts
```

---

## 3. 当前分支没有获得正常 GitHub PR 验证

目前没有检索到这个分支对应的 PR。

而 Build/Test workflow 只在：

* pull_request；
* push main；

运行。

所以“本地完成”不能等同于“GitHub checks 已通过”。下一步应先开 PR，让：

* Debug；
* Release；
* Quality；
* QML；
* clang-tidy；

全部实际运行。

---

## 4. MSI 快捷方式组件的 Registry KeyPath 建议调整

当前是 per-machine MSI，但快捷方式组件使用了 HKCU registry value 作为 KeyPath。

这在安装账户上可能正常，但多用户环境的 repair/self-heal 行为容易复杂化。

建议：

* per-machine component 使用 HKLM KeyPath；
* 或使用标准的 shortcut component keypath 方案；
* 测试另一个 Windows 用户是否能看到并启动开始菜单快捷方式。

---

# 五、Bad Case 导出评估

Bad Case 导出已经实现：

```text
VCStation-badcase-...
├── comparison.bmp
└── evidence.json
```

目录先写临时路径，成功后整体 rename，失败会清理临时目录，原子性处理是正确的。

JSON 包含：

* canonical frame；
* requested frame；
* session epoch；
* playback generation；
* alignment revision；
* source frame；
* match kind；
* confidence。

### 建议改进

1. `BMP` 改为 `PNG`，否则 1080p/4K Bad Case 占用过大。
2. JSON 同时保存：

   * `canonical_frame_zero_based`
   * `canonical_frame_display = zero_based + 1`
3. 记录当前：

   * view mode；
   * difference edge；
   * metric；
   * gain；
   * threshold；
   * wipe position；
   * ROI。
4. 增加真实 WARP/GUI 导出测试，而不是只给 exporter 一个合成 QImage。当前测试只验证文件写入和 JSON，并没有证明 `grabToImage()` 能可靠捕获生产 D3D11 画面。

这属于 P1。

---

# 六、Shutdown soak 名称大于实际覆盖范围

新增的 `ShutdownSoak.ps1` 会运行 20 次：

```text
启动
→ 在 open 过程中退出
→ 检查 10 秒内结束
```

它确实是一个有效的 shutdown-during-open 重复测试，但还不能称为完整 soak，因为没有覆盖：

* 播放中退出；
* 快速 seek 中退出；
* alignment 分析中退出；
* 保存项目中退出；
* 多轮打开/关闭同一进程；
* device loss；
* 线程数和 handle 数增长。

建议重命名当前测试为：

```text
shutdown-during-open-soak
```

再补充真正的：

```text
session-lifecycle-soak
```

---

# 七、维护性风险

## Main.qml 已经过度膨胀

这次 `Main.qml` 同时承载：

* 菜单；
* drag/drop；
* 确认对话框；
* source cards；
* toolbar；
* alignment inspector；
* viewport；
* Wipe；
* ROI；
* Loading；
* transport；
* timeline；
* project dialogs；
* Bad Case dialogs。

功能正确，但后续迭代会越来越难。

建议拆分为：

```text
Main.qml
├── SourceBar.qml
├── DropOverlay.qml
├── DropConfirmationDialog.qml
├── ComparisonToolbar.qml
├── AlignmentInspector.qml
├── ComparisonViewport.qml
├── TransportBar.qml
├── TimelineBar.qml
└── WorkspaceDialogs.qml
```

Main 只负责布局和顶层状态连接。

这不是当前合并阻断项，但应作为下一轮重构的第一个 P1。

---

# 八、合并前的明确修复清单

## P0：必须修复

1. 将版本改为 `1.1.0`，并从 CMake 自动生成 RC 版本。
2. 修复 `1080p120-2source` 的 `backends.size() == 3`。
3. 修复 `serviceError(sourceId=0)` 丢失 Source A attribution。
4. 把旧 DualVideoStudio 1.0.0 MSI 接入真实升级测试。
5. Release workflow 校验 tag、PROJECT_VERSION、RC 和包文件名一致。
6. Release workflow 要求同一 commit 的 Quality、Hardware 和 Performance 结果。
7. 建立 PR 并跑完 GitHub Debug、Release、Quality checks。

## P1：建议在 1.1.0 前完成

1. 单视频拖放填充下一个空 slot。
2. Source 卡片支持定向拖放替换。
3. 统一中英文和翻译体系。
4. 给横向工具栏增加滚动提示，进一步减少顶部控件。
5. Bad Case 改 PNG，并记录完整 view state。
6. 增加实际 GPU Bad Case capture 测试。
7. 将当前 shutdown 测试准确命名并扩展 lifecycle soak。
8. 迁移旧 `%LocalAppData%\DualVideoStudio` 设置到 `%LocalAppData%\VCStation`。当前路径已经直接切换到新目录，但没有看到旧设置迁移逻辑。

## P2：后续优化

1. 真正事件式 provider pending-set assembly。
2. byte-aware prefetch depth。
3. Signature cache 改为 byte-LRU。
4. 拆分 Main.qml。
5. 清理已经不参与发布的 Gyan FFmpeg executable manifest。

---

# 最终判断

这次提交的总体质量明显高于普通 UI 补丁。尤其是以下部分值得保留：

* 拖放验证放在 C++ 而非依赖扩展名；
* Wipe 真正进入 D3D renderer 并有像素测试；
* `busy` 与 `framePending` 分离；
* Snapshot 改为事件驱动；
* MSI 开始菜单和文件关联有真实安装测试；
* 用户包禁止携带外部 ffmpeg/ffprobe；
* Signature cache 已针对短视频工作负载设置边界；
* Bad Case 采用临时目录后原子发布。

但是，**当前状态应定义为“功能实现完成、合并验收尚未完成”**。最关键的下一步不是再增加功能，而是修正版本升级、两路 120 FPS 门禁和 Source 0 错误归属，然后通过一个正式 PR 完成全部 CI 与真实 MSI 升级验证。
