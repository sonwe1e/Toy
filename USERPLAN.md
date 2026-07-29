# 执行状态（2026-07-30）

本文件主体保留了最初对提交 `9fe65be7` 的审计和实施依据；其“尚未完成”描述属于当时
快照。当前已按推荐顺序完成阶段 1～6 的本地实现与验证，GitHub checks 按用户要求作为
最后任务单独收口。

当前验证基线：

* Debug 360/360、Release 360/360 常规测试通过；
* format-check、qmllint、clang-tidy 通过；
* 2/2 D3D11VA/decoder-surface hardware CTest 通过；
* 三路 1080p60 与三路 4K30 Main10 均完成 300 秒真实可见窗口门禁；
* 两组门禁均无 source split、无线程增长、无预算泄漏，seek P95 和关闭时限达标；
* hardware/performance CTest 与统一 PowerShell runner 入口已经落地；
* ZIP/MSI 已重新生成，包内 CLI/GUI smoke、MSI administrative extraction 和无警告
  WiX 数据库验证均通过；
* Windows self-hosted runner 已完成注册、双标签和外部性能素材配置；最新提交的 GitHub
  checks 作为本计划的最终发布门禁。

---

# 原始审计结论

以最新分支头提交 `9fe65be7` 为准，项目已经完成了一次质量较高的架构迁移：它不再是把三个旧播放器简单拼在一起，而是形成了一个 **Windows 原生、2～3 路、帧精确、原子发布、支持显式时间对齐和 GPU 差分的 VFI 比较器**。当前 PR 仍为 Draft，共包含 12 个提交和 415 个变更文件。

相比上一笔提交，最新代码已经实质性解决了几个核心问题：

* canonical source 不再默认等于第一路，而是显式传给媒体适配器；
* 解码失败不再伪装成 `Missing frame`；
* 每路视频使用持久化 `SourceDecodeActor`，不再逐帧创建线程；
* 对齐分析使用独立服务、独立解码 provider 和独立内存预算；
* 完整 O(N) 对齐表不再进入 16 ms Snapshot 路径；
* 差分边缺帧时改为黑色不可用状态，不再显示单路原图。

因此，当前项目**不需要再次推倒重写**。接下来的正确路线应当是：

> 先封板 UI 与 CI 正确性，再完成媒体运行时缓存、真实预取、对齐鲁棒性和项目状态持久化，最后再进入 D3D11VA、P010 与三路 1080p60 性能阶段。

当前成熟度可以概括为：

| 方面     | 判断                   |
| ------ | -------------------- |
| 核心架构   | 阶段 0～6 已完成             |
| 多路帧原子性 | 已通过持续硬件门禁             |
| 逐帧审查体验 | 动态 2～3 路、项目闭环已完成       |
| 对齐算法   | 时间引导、分段置信度、anchors 已完成 |
| 解码调度   | Actor、缓存、预取闭环已完成       |
| GPU 分析 | 高级分析和 exact-plane diff 已完成 |
| 项目持久化  | schema v3 与 GUI 闭环已完成     |
| 媒体兼容性  | 8/10-bit SDR 与 D3D11VA 已完成 |
| CI/发布  | 本地门禁完成，self-hosted checks 为最终门禁 |

---

# 一、当前整体架构

## 1. 模块边界

当前根工程采用一个受约束的模块化单体：

```text
DualVideoStudio / DualVideoStudioCli
        │
        ├── ui_qml
        ├── media_ffmpeg
        ├── persistence_json
        └── platform_windows
                 │
            application
                 │
               domain
```

根 CMake 只构建：

* `domain`
* `application`
* `platform_windows`
* `media_ffmpeg`
* `persistence_json`
* `ui_qml`
* `app`

旧的 `DualVideoTool` 和 `video-compare` 已经放入 `legacy/`，不再参与主构建。

这一点做得很好：项目没有继续维护三套播放器时钟、三套 UI 和三套解码状态机。

架构约束也不是只写在文档里。`Architecture.cmake` 会在配置阶段检查：

* `domain` 不能依赖任何项目模块；
* `application` 只能依赖 `domain`；
* core 禁止引入 Qt、FFmpeg、JSON、D3D11 等框架类型；
* adapter 不能形成反向依赖；
* generator expression 不能隐藏项目内部依赖。

这是当前工程长期可维护性的主要保障。

### 仍需注意的结构风险

`platform_windows` 当前同时承载：

* D3D11 device；
* GPU frame；
* frame budget；
* mailbox；
* deadline scheduler；
* 文件路径和原子文件发布；
* source identity；
* GPU transfer；
* render channel。

它已经成为多个 adapter 的公共基础设施包。短期不用拆，但必须防止它逐渐变成新的“大泥球”。Phase 6 后建议拆成：

```text
platform_windows_core
├── WindowsPaths
├── SourceIdentity
├── AtomicFilePublisher
└── SteadyDeadlineScheduler

graphics_d3d11
├── GraphicsDeviceBroker
├── FrameMailbox
├── GpuTransferActor
├── FrameResource
└── D3d11ComparisonRenderer
```

---

## 2. 控制面与数据面已经分离

当前存在两条不同的数据路径。

### 低频控制路径

```text
QML
  ↓
ReviewController
  ↓
PlaybackCoordinator
  ↓
Commands / ApplicationEvents / SessionSnapshot
```

它负责：

* 打开源；
* probe；
* seek；
* play/pause；
* 对齐命令；
* 错误；
* 请求身份；
* generation；
* UI 状态。

### 高频帧路径

```text
SourceDecodeActor × 2/3
  ↓
FrameSetAssembler
  ↓
FrameSet
  ↓
D3d11RenderChannel
  ↓
GpuTransferActor
  ↓
FrameMailbox
  ↓
D3d11ComparisonRenderer
  ↓
PresentationAck
```

帧资源不会成为 QML property。QML 只能看到已经完成 presentation acknowledgement 的帧编号和 source frame identity。

这符合视频比较器最重要的约束：

> Reference、Prediction 1、Prediction 2 不能独立推进；只能整组推进。

`FrameSet` 允许某一路显式 `Missing`，但要求每个已加载 source 都有一个 slot。Missing 必须携带 `AlignmentGap`、`BeforeSourceStart` 或 `AfterSourceEnd`，普通 decoder failure 不能伪装成 Missing。

---

## 3. 解码链已经完成 Actor 化

每一个 source 都有一个长期存活的 `SourceDecodeActor`：

```text
SourceDecodeActor
├── control queue
├── exact queue       capacity 1
├── sequential queue  capacity 2
├── prefetch queue    capacity 8
├── analysis queue    capacity 1
└── SoftwareDecoder   独占
```

其优先级为：

```text
Control > Exact > Sequential > Prefetch > Analysis
```

Exact 请求会清理旧 Prefetch；旧 playback generation 的排队请求也会被取消。

所有实际 decode 都运行在 actor 的稳定 worker thread 上，不再使用逐帧 `std::async` 创建新线程。

上层将所有 actor 的 future 汇合，再通过 `FrameSetAssembler` 恢复 session source order，只有每个 slot 都有终态后才创建 `FrameSet`。

这条路线是正确的。

---

## 4. 对齐分析已经从播放后端独立出来

`AlignmentAnalysisService`：

* 有独立低优先级 worker；
* 有独立 queue；
* 使用独立 `MultiSourceFrameProvider`；
* 使用独立 32 MiB analysis frame budget；
* 支持 Started、Progress、Completed、Canceled、Failed；
* 可以在播放和逐帧审查期间后台运行。

协调器不会立即应用后台分析结果。它等待当前没有 seek、播放帧或 graphics pending，再将结果原子应用并重新呈现当前帧。

这避免了分析结果在播放中途突然改变三路映射。

---

# 二、当前做得最好的技术决策

## 1. canonical source 显式化

`FrameProviderOpenRequest` 已加入 `canonicalSourceId`，并明确禁止 adapter 从 `sources.front()` 推断。

这意味着 Reference 可以是 A、B 或 C，prediction-only 时才使用第一路作为 canonical。该设计已经消除了上一版最危险的时间线错误。

## 2. 对齐状态是可审计的

每一路帧都携带：

* `ExactIndex`
* `GlobalOffset`
* `AutoAligned`
* `ManualAnchor`
* `Missing`
* confidence

因此播放器可以明确告诉使用者当前显示的是严格同帧、自动对齐帧还是手工锚点帧，而不是静默移动预测结果。

## 3. Snapshot 已经完成轻量化

完整 `SequenceAlignmentResult.entries` 不再复制进 Snapshot。Snapshot 只保留：

* bounded anomalies；
* bounded low-confidence runs；
* overall cost/confidence；
* alignment revision；
* analysis progress。

完整 O(N) map 只保留在 coordinator 内部。这个修改有效避免了每帧显示时复制 50,000～100,000 个 mapping entry。

## 4. 差分缺帧不再产生误导

最新版 renderer 在差分边缺少任一路时直接绘制黑色，而不是退化成单路画面；QML 同时构造了明确的 “Frame N cannot be compared because Source X is missing” 信息。

这对盲测和 VFI bad-case 检查非常重要。

---

# 三、目前最需要立即修复的问题

## P0-1：QML 存在同名属性重复声明

`Main.qml` 的根 `Window` 中两次声明了：

```qml
readonly property bool manualAlignmentActive
```

第一处代表 controller 中的手动锚点状态，第二处代表三个 offset SpinBox 是否非零。

这不仅是 QML 名称冲突，还说明产品概念被混在一起了：

* 手动全局 offset；
* 手动 alignment anchor。

### 修改方案

拆成三个属性：

```qml
readonly property bool manualAnchorActive:
    Boolean(controller && controller.manualAnchorActive)

readonly property bool manualOffsetActive:
    sourceAOffset.value !== 0
    || sourceBOffset.value !== 0
    || sourceCOffset.value !== 0

readonly property bool anyManualAlignmentActive:
    manualAnchorActive || manualOffsetActive
```

对应按钮和状态标签也分别显示：

```text
Manual offset active
Manual anchors active
```

必须增加一个 QML contract test，直接实例化 `Main.qml`，而不仅测试单独的 `ToolbarCombo.qml`。

---

## P0-2：GitHub CI 仍未完成真实验证

最新 `Quality` workflow 已失败，`Build and Test` 仍在排队。

失败原因已经明确：`check-repository-guide.ps1` 要求指南包含 200～400 个词，当前为 406 个词。由于 `native-quality` 依赖该 job，format 和 lint 整个被跳过。

发布说明虽然记录了本地 Debug/Release 299 个测试和本地 format/lint 通过，但 GitHub 并未验证最新提交。

### 修改方案

第一步，删减 `AGENTS.md` 至 400 词以内。

第二步，解除质量 job 的串行耦合：

```yaml
jobs:
  repository-guide:
    ...

  native-quality:
    # 不要 needs: repository-guide
    ...
```

或者：

```yaml
needs: repository-guide
if: always()
```

前者更合适，因为文档长度失败不应阻止编译和静态分析运行。

第三步，建立必须通过的 branch protection：

```text
Repository guide
Native format and lint
Debug build and test
Release build and test
Package verification
GUI smoke
```

在上述状态全部绿色之前，不应将 `0.1.0` 视为稳定发布。

---

# 四、媒体解码架构的主要不足

## 1. Provider worker 仍会同步等待所有 future

解码本身已经并行，但 `MultiSourceFrameProvider` 的 worker 会依次执行：

```cpp
for (SlotDecode& slot : decoding) {
    auto decoded = slot.result.get();
}
```

所以：

[
T_\text{FrameSet}\approx\max(T_A,T_B,T_C)
]

这一点没有问题，但 provider 的控制线程在等待期间不能处理下一项 control operation，只能依靠 cancellation flag 和 FFmpeg interrupt 中断 actor。

对于 2～3 路当前规模可以接受，但继续增加预取、硬件 fallback 或并行分析后会成为限制。

### 目标方案：事件式 FrameSet assembly

actor 不再通过 future 返回，而是向 provider completion mailbox 发布：

```cpp
struct SourceDecodeCompleted {
    OperationId operationId;
    SourceId sourceId;
    Result<DecodedFrame> result;
};
```

Provider 保存：

```cpp
unordered_map<OperationId, PendingFrameSet> pendingSets;
```

处理流程：

```text
submit frame
  ↓
为每个 actor 发送 request
  ↓
provider worker 立即返回事件循环
  ↓
actor completion 进入 mailbox
  ↓
FrameSetAssembler.complete()
  ↓
全部 slot 完成后发布 FrameSet
```

好处是：

* provider 控制线程永远不等待 decoder；
* close/cancel/open 可以及时进入控制面；
* 容易加入 per-source timeout；
* 容易记录每路 decode latency；
* 硬件解码 fallback 不会阻塞整个 provider event loop。

---

## 2. 当前 FrameSet cache 几乎无法跨请求复用

`cachedSet()` 的 key 同时比较：

* 完整 `FrameRequestContext`
* canonical frame

而 `FrameRequestContext` 内包含 `RequestId`。不同 seek 命令即使请求同一帧，也会有不同 context，因此无法 cache hit。

这会影响：

* A/D 往返逐帧；
* 时间线拖动后返回上一帧；
* 差分模式切换；
* 反复定位 bad case；
* 未来 Prefetch。

### 正确的缓存层次

不要把完整 `FrameSet` 作为唯一缓存层。

#### Source frame cache

```cpp
struct SourceFrameCacheKey {
    SourceFingerprint source;
    FrameId sourceFrame;
    NormalizationProfile profile;
};
```

每路 actor 拥有一个 byte-budgeted LRU。

#### FrameSet mapping cache

```cpp
struct FrameSetCacheKey {
    SessionEpoch sessionEpoch;
    uint64_t alignmentRevision;
    FrameId canonicalFrame;
};
```

映射 revision 一旦改变，旧 FrameSet 自动失效。

#### GPU cache

GPU frame 必须再包含：

```cpp
DeviceGeneration deviceGeneration;
```

不能跨 device loss 重用。

---

## 3. Prefetch 数据结构存在，但调度尚未闭环

Actor 和 provider 都定义了 Prefetch queue，但当前 coordinator 的帧请求路径主要产生 Exact 和 Sequential，尚未形成真正的 prefetch scheduler。

### 推荐策略

暂停状态下每次 exact frame 成功后：

```text
用户方向向前：
  +1, +2, +3, -1

用户方向向后：
  -1, -2, -3, +1
```

连续播放时：

```text
维持未来 2～4 个 canonical FrameSet 的 source frames
```

跳转或 alignment revision 改变时：

```text
清除旧 generation 的 Prefetch
```

Prefetch 只填充 SourceFrameCache，不应提前构造和长期持有完整 GPU FrameSet。

---

## 4. Probe、播放 decoder 和分析 decoder 重复建立 PTS index

每个 `SoftwareDecoder::open()` 都重新调用 `buildPresentationTimestampIndex()`。

因此一次三路会话可能执行：

```text
MediaProbe        扫描 3 路
Playback decoder  再扫描 3 路
Alignment job     再扫描 3 路
第二次分析        再扫描 3 路
```

对于长视频，这是当前最明显的启动和分析成本之一。

### 具体方案：媒体运行时索引注册表

在 `media_ffmpeg` 内新增：

```cpp
class PresentationIndexCache {
public:
    shared_ptr<const PresentationIndex>
    findOrBuild(const MediaDescriptor&, CancellationToken);
};
```

Key：

```text
source fingerprint
+ selected stream index
+ file size
+ modified time
+ FFmpeg major/minor
+ index schema version
```

磁盘格式：

```text
magic
schemaVersion
sourceFingerprint
streamIndex
timeBase
frameCount
PTS[int64 × N]
checksum
```

文件写入使用：

```text
temporary file → flush → verify → atomic rename
```

`MediaProbe`、playback decoder 和 analysis decoder 都通过该 cache 获取同一个 immutable index。

---

## 5. 每帧仍创建 SwsContext 和临时 NV12 vector

当前成功 decode 后仍执行：

```cpp
std::vector<uint8_t> nv12(...);
sws_getContext(...);
sws_scale(...);
```

即每帧重新创建 scaler 和中间 buffer。

### 修改方案

在 `SoftwareDecoder::Impl` 内持久保存：

```cpp
SwsContextPtr sws;
Nv12BufferPool bufferPool;
NormalizationProfile activeProfile;
```

使用：

```cpp
sws_getCachedContext(...)
```

并让 `FrameResourceFactory` 返回可写 resource：

```cpp
auto resource = factory.acquireWritableNv12(layout);
sws_scale(..., resource.yPlane(), resource.uvPlane());
return resource.seal();
```

这样可以删除中间 `std::vector` 和二次复制。

---

# 五、对齐分析架构仍有明显优化空间

## 1. 独立分析服务仍复用了完整播放 provider

`AlignmentAnalysisService` 内部创建了一个低优先级 `MultiSourceFrameProvider`。

这意味着分析一帧的路径是：

```text
FFmpeg AVFrame
  ↓
转换完整 NV12
  ↓
申请 FrameBudget
  ↓
构造 FrameHandle
  ↓
再从 NV12 下采样到 16×9
```

而分析真正需要的只是 144 个亮度值。

### 目标架构

新增专用分析解码器：

```cpp
class SignatureDecodeSession {
public:
    Result<FrameLumaSignature> decodeSignature(FrameId);
    Result<vector<FrameLumaSignature>> decodeRange(...);
};
```

直接从 `AVFrame::data[0]` 或原始 luma plane 下采样，不创建：

* `FrameHandle`
* NV12 FrameResource
* GPU transferable frame
* FrameSet
* render cache

这样可以把长视频分析的内存和拷贝量降低一个数量级。

随后可从 application port 删除以下重复能力：

```cpp
IFrameProvider::submit(AlignmentEstimateRequest)
IFrameProvider::submit(SequenceAlignmentRequest)
```

保留单一职责：

```text
IFrameProvider             只负责播放帧
IAlignmentAnalysisService  只负责分析
```

当前两个接口同时暴露同一分析 request，协调器也保留了无分析服务时回退到 direct provider 的旧路径，这会长期增加状态机分支。

建议删除旧 fallback 后，一并移除：

* `PendingPhase::kEstimatingAlignment`
* `PendingPhase::kAnalyzingSequence`
* `AlignmentEstimated`
* `SequenceAlignmentAnalyzed`

只保留 job 型 analysis events。

---

## 2. Global alignment 的进度语义不正确

analysis service 的 `totalFrames()` 直接累加所有视频完整帧数。

但 global offset 实际只解码少量 sample；目前 provider 的 progress emitter 也只针对 `SequenceAlignmentRequest`。

结果可能是：

```text
Auto align: 0 / 100000
```

然后突然完成。

### 修改方案

把“frames”改成通用 work units：

```cpp
struct AlignmentWorkEstimate {
    uint64_t totalUnits;
    string unitName; // "samples" or "frames"
};
```

Global：

```text
totalUnits =
canonical samples
+ 所有目标候选 sample 数
```

Sequence：

```text
totalUnits =
所有 source frame counts
+ DP work units
```

同时在完成 signature extraction 后继续报告 DP 阶段：

```text
Collecting signatures  75%
Computing alignment    25%
```

---

## 3. 分析服务每个 job 都重新打开媒体

每个 job 都会：

1. 创建 `FrameProviderOpenRequest`；
2. 等待 open；
3. 提交分析；
4. 等待 terminal。

任务结束后没有显式 close，资源会一直保留到下一次 open 或 service shutdown。

### 修改方案

短期应至少使用 RAII：

```cpp
class ScopedAnalysisSession {
public:
    open();
    ~ScopedAnalysisSession() { close(); }
};
```

长期则使用 signature cache：

```text
第一次 Find drops：
  decode + signature extraction + cache

第二次重新调整 band/gap penalty：
  直接读取 signature cache，只重新运行 DP
```

Signature cache key：

```text
source fingerprint
+ rotation/SAR normalization
+ signature resolution
+ signature algorithm version
+ FFmpeg normalization version
```

---

## 4. 16×9 signature 对真实游戏视频仍偏弱

当前 signature 只有 16×9 亮度值。

它适合快速粗搜索，但容易被以下内容干扰：

* 黑场；
* 固定 HUD；
* 重复菜单；
* 大面积静止背景；
* 周期性动画；
* 小范围快速运动；
* Prediction 中只有局部插帧错误。

### 建议采用两级分析

#### Level 1：粗搜索

```text
16×9 luma
pHash
global gradient
```

用于搜索 offset 和 DP 初始路径。

#### Level 2：候选验证

```text
64×36 luma
Sobel magnitude
局部方差
8×8 block histogram
temporal difference
```

只对 Level 1 的最佳候选和 runner-up 计算。

最终距离可以为：

[
D =
w_1 D_{\text{SSIM}}
+w_2 D_{\text{gradient}}
+w_3 D_{\text{temporal}}
+w_4 D_{\text{block}}
]

同时允许用户指定忽略区域，例如屏蔽固定 UI 或黑边。

---

## 5. 当前 DP 路径只适合近似相同帧率

现有 sequence alignment 的 band center 是：

[
j \approx i+\delta
]

这适合：

* 差一两帧；
* 全局偏移；
* 中间掉帧；
* 重复帧。

但不适合：

* 24 FPS 与 30 FPS；
* 30 FPS 与 60 FPS；
* 手机录屏中持续时间漂移；
* 录屏开始和结束存在不同步；
* 长视频中逐渐累积的 drift。

当前 validator 对帧率、帧数和时长不一致仍全部标记为普通 Warning，`AlignmentRequired` 实际没有被使用。

### 正确方案：时间引导的 band

为每个 canonical frame 计算初始目标：

[
t_i=T_\text{canonical}(i)
]

[
j_0(i)=T_\text{target}^{-1}(t_i+\Delta t)
]

DP band 改成：

[
j\in[j_0(i)-W,\ j_0(i)+W]
]

而不是固定围绕 (i+\delta)。

这样才能支持 VFR 和轻微速度漂移。

### 兼容性等级也应调整

| 情况           | Severity                    |
| ------------ | --------------------------- |
| 单个视频打不开、无法索引 | Fatal                       |
| 帧率、帧数、持续时间不同 | AlignmentRequired           |
| 分辨率、颜色元数据不同  | Warning                     |
| 旋转/SAR 未处理   | AlignmentRequired 或 Warning |
| HDR/位深无法归一化  | Fatal                       |

在 `AlignmentRequired` 尚未完成前，UI 应禁用“自动应用”，只允许 Strict Index 和手动 offset。

---

## 6. 自动应用仍是整张表的全有或全无

当前 `autoApplicable` 由整体平均 match cost 和平均 confidence 决定。之后整张 result 被应用或完全不应用。

这可能出现：

```text
90% 画面非常可靠
10% 重复场景高度歧义
平均 confidence 仍然通过
整张 mapping 被自动应用
```

### 修改方案：分段对齐状态

```cpp
enum class AlignmentSegmentState {
    Accepted,
    ReviewRequired,
    Rejected,
};
```

每个 segment 评估：

* mean confidence；
* P10 confidence；
* 最大连续低置信区间；
* anomaly density；
* scene-cut proximity；
* mapping slope。

最终允许：

```text
0–824       Accepted
825–910     ReviewRequired
911–2000    Accepted
```

`ReviewRequired` 区间保持 strict/global-offset 映射，直到用户确认。

---

## 7. 手动锚点应成为 DP 的硬约束

当前手动 anchors 是在最终 `sourceMappingsFor()` 中覆盖自动 map，而不是在 DP 中约束路径。

更好的算法是：

1. 按 anchor 将时间线切成多个区间；
2. 每个区间的起点和终点固定；
3. 在区间内部执行 banded DP；
4. 确保路径经过所有 anchor；
5. anchor 冲突时报告而不是强行覆盖。

这样自动 map 与手动 map 会形成一张一致的单调路径。

---

# 六、UI 结构仍然没有真正动态化

核心已经是 `vector<ComparisonSource>`，但 UI 仍是：

* `sourceAFilename`
* `sourceBFilename`
* `sourceCFilename`
* `sourceAErrorKey`
* 三个 offset 参数
* 固定 A/B、A/C、B/C edge。

对于最多三路来说可以运行，但它会让所有 UI 状态继续硬编码。

### 建议新增 `SourceListModel`

```cpp
class SourceListModel : public QAbstractListModel {
    enum Roles {
        SourceIdRole,
        RoleRole,
        FilenameRole,
        ErrorRole,
        CurrentSourceFrameRole,
        MatchKindRole,
        ConfidenceRole,
        MissingRole,
        MissingReasonRole,
        ManualOffsetRole,
    };
};
```

QML 使用：

```qml
Repeater {
    model: controller.sources
}
```

差分 edge 也应由模型动态生成：

```text
A ↔ B
A ↔ C
B ↔ C
```

而不是固定 enum 和固定三个 ComboBox item。

这也会自然解决：

* 第三路不存在时隐藏 C；
* Reference 不是 A；
* source display name；
* source-specific compatibility warning；
* 未来扩展第四路时不需要重写 UI。

---

# 七、CompatibilityReport 在 UI 边界损失了信息

领域层的 `CompatibilityFinding` 包含：

* severity；
* error code；
* 具体 source IDs；
* technical detail。

但 Snapshot 只保留 `vector<MediaErrorCode>`。因此 UI 不知道：

* 是 A 与 B 不一致，还是 A 与 C；
* 同一 warning 是否出现两次；
* severity 是 Warning 还是 AlignmentRequired。

### 修改方案

新增：

```cpp
struct CompatibilityFindingView {
    CompatibilitySeverity severity;
    MediaErrorCode code;
    vector<SourceId> sources;
};
```

Snapshot 持有 bounded findings：

```cpp
vector<CompatibilityFindingView> compatibilityFindings;
```

UI 显示：

```text
A ↔ C: frame rate mismatch — alignment required
B ↔ C: resolution mismatch — resampled visual comparison
```

---

# 八、项目持久化目前没有形成真实产品闭环

仓库中保留了：

* `Project`
* `ProjectJson`
* `ProjectRepository`
* schema v2；
* relink；
* fingerprint。

但当前 GUI composition 只注入了 `SettingsRepository`，`PlaybackCoordinator::Dependencies` 中也没有 `ProjectRepository`。

因此项目存取代码目前更像“编译中的未来模块”，而不是 GUI 可用能力。

Schema v2 也只保存：

* sources；
* Reference；
* marks；
* last frame；
* workspace。

没有保存 offsets、anchors 和 alignment policy。

### 必须先做产品决策

#### 路线 A：0.1 只做临时比较器

从生产构建中移除未接线的 ProjectRepository，减少维护面。

#### 路线 B：正式支持可恢复审查项目

新增独立 `WorkspaceCoordinator`，不要继续把所有项目逻辑塞进 `PlaybackCoordinator`。

Schema v3 建议保存：

```json
{
  "alignment": {
    "mode": "manual-anchors",
    "offsets": [
      { "sourceId": 1, "frames": 1 }
    ],
    "anchors": [
      {
        "sourceId": 2,
        "canonicalFrame": 824,
        "sourceFrame": 825
      }
    ],
    "analysisCacheKey": "..."
  },
  "view": {
    "layout": "reference-focus",
    "differenceEdge": [0, 2],
    "differenceMetric": "heatmap",
    "gain": 4
  }
}
```

完整 sequence map 不应写入 JSON；只保存派生 cache key，并根据 source fingerprint 和算法版本重新验证。

---

# 九、差分图需要明确区分“视觉差分”和“像素精确差分”

当前 shader 先将两路 NV12 独立转换到 RGB，再计算 RGB、Luma、Chroma 或 Heatmap。

因此当前模式本质上是：

> display-RGB visual difference

它不一定等同于源像素 code-value exact difference。

### 建议定义两类模式

#### Visual Diff

允许：

* 分辨率不同；
* BT.601/BT.709 转换；
* range 转换；
* spatial resampling；
* bicubic。

#### Exact Plane Diff

要求：

* 相同 resolution；
* 相同 pixel format；
* 相同 bit depth；
* 相同 rotation/SAR；
* ExactIndex；
* 不进行 spatial resampling。

直接比较：

```text
Y code values
U code values
V code values
```

新增状态：

```cpp
enum class ComparisonExactness {
    ExactCodeValue,
    DisplaySpaceConverted,
    SpatiallyResampled,
    TemporallyAligned,
    Unavailable,
};
```

在 UI 中持续显示 badge，而不是只显示一条临时 warning。

---

# 十、Phase 5 尚未完成的分析能力

架构文档已经明确列出尚缺：

* threshold mask；
* ROI zoom；
* complete analysis grid。

推荐完成顺序如下。

## 1. 同步 pan/zoom

所有 panel 使用同一个 normalized viewport：

```cpp
struct ViewTransform {
    float centerX;
    float centerY;
    float scale;
};
```

不同分辨率按 normalized coordinates 映射。

## 2. 1:1 pixel 模式

选择 reference canvas 后：

* Reference 保持一个 source pixel 对一个 screen pixel；
* Prediction 使用 nearest 或明确标注 resampling；
* 鼠标显示每路像素值。

## 3. Threshold mask

```text
abs difference < threshold → black
abs difference ≥ threshold → highlight
```

阈值应支持：

* 8-bit code value；
* normalized float；
* luma-only；
* any-channel；
* all-channel。

## 4. Analysis grid

推荐四格而不是六格：

```text
┌──────────────┬──────────────┐
│ Reference    │ Prediction 1 │
├──────────────┼──────────────┤
│ Prediction 2 │ Selected Diff│
└──────────────┴──────────────┘
```

这样比同时显示三张误差图更适合 1080p/4K 屏幕。

---

# 十一、媒体兼容性和硬件路线

当前正式支持仍是：

* H.264；
* HEVC；
* MPEG-4 Part 2；
* 8-bit YUV420；
* SDR；
* BT.601/BT.709；
* 软件解码。

后续路线不应直接跳到 D3D11VA，而应按以下顺序。

## Phase 6A：软件路径性能封板

完成：

* PTS index cache；
* signature cache；
* `sws_getCachedContext`；
* writable FrameResource pool；
* SourceFrameCache；
* real Prefetch；
* performance telemetry。

## Phase 6B：格式归一化

内部 frame profile：

```cpp
enum class NormalizedFrameFormat {
    Nv12_8,
    P010_10,
    Bgra8,
};
```

支持：

* 10-bit；
* P010；
* YUV422/YUV444 转换；
* RGB/BGRA；
* rotation；
* SAR；
* full/limited range；
* transfer metadata。

## Phase 6C：D3D11VA

每路 decoder：

```text
AVHWDeviceContext
       ↓
D3D11VA frame
       ↓
shared D3D11 texture
       ↓
renderer
```

避免：

```text
GPU decode → CPU NV12 → GPU upload
```

必须保留：

* software fallback；
* fallback 原因；
* 当前 decoder backend；
* device loss generation；
* P010 shader；
* hardware-specific integration tests。

---

# 十二、测试与性能门禁

发布说明记录本地 299/299 Debug 和 Release 测试通过，但 README 仍明确说明 hardware、performance 和 shutdown-soak 层目前没有真实测试。

下一阶段应增加以下真实测试。

## 正确性

* Reference 为 B/C；
* Reference 为 VFR；
* 帧率不同但 Strict Index 打开；
* decoder 中间坏帧必须失败，不得 Missing；
* alignment gap、before start、after end 三种 Missing；
* source file 在 probe 后变化；
* device loss 后旧 GPU frame 不复用；
* Difference unavailable；
* QML 根组件完整加载。

## 对齐

* 黑场；
* 重复场景；
* scene cut；
* 固定 HUD；
* 手机 VFR 录屏；
* 30→60 FPS；
* 全局 offset + 中间掉帧；
* 两个 anchors 约束 DP；
* 局部低置信区间；
* 100,000 帧取消；
* signature cache 命中与失效。

## 性能

至少采集：

```text
probe/index duration
cold exact seek p50/p95
warm ±1 step p50/p95
per-source decode latency
FrameSet assembly latency
cache hit ratio
CPU→GPU upload time
render time
dropped complete FrameSet count
analysis frames/s
peak CPU/GPU memory
worker thread count
```

硬件验收目标应包括：

```text
3 × 1080p60 连续播放 5 分钟
3 × 4K30 连续播放 5 分钟
无 source split
无线程数量持续增长
无帧资源预算泄漏
关闭过程符合既定 7 秒上限
```

---

# 十三、推荐的落地实施顺序

## 阶段 1：合并门禁封板

修改：

```text
src/ui_qml/qml/Main.qml
AGENTS.md
.github/workflows/quality.yml
tests/component/ui/
```

完成：

* 修复重复 `manualAlignmentActive`；
* 拆分 offset 与 anchor 状态；
* 修复 406 词门禁；
* 让 native-quality 独立运行；
* 增加完整 Main.qml load test；
<!-- * 确保所有 GitHub checks 绿色。 -->

## 阶段 2：播放性能闭环

新增：

```text
SourceFrameCache
FrameSetCacheKey
PrefetchScheduler
PresentationIndexCache
Nv12BufferPool
```

修改：

```text
SoftwareDecoder
SourceDecodeActor
MultiSourceFrameProvider
PlaybackCoordinator
```

完成：

* 缓存 key 不再包含 requestId；
* 实现方向感知 Prefetch；
* 复用 PTS index；
* 复用 SwsContext；
* 删除中间 NV12 vector。

## 阶段 3：分析解码专用化

新增：

```text
SignatureDecodeSession
SignatureCache
AlignmentWorkEstimator
```

删除：

```text
IFrameProvider 中的分析方法
PlaybackCoordinator 的旧 foreground analysis fallback
旧 AlignmentEstimated / SequenceAlignmentAnalyzed 事件
```

完成：

* 直接从 AVFrame 提取 signature；
* 每个 job 显式 close；
* global/sequence 都有准确进度；
* 第二次分析不重新解码。

## 阶段 4：对齐鲁棒性

完成：

* 多尺度 signature；
* 时间引导 band；
* VFR target timeline；
* anchors 约束 DP；
* scene cut；
* segment-level confidence；
* AlignmentRequired severity；
* 用户确认/撤销自动结果。

## 阶段 5：动态 UI 与项目闭环

完成：

* `SourceListModel`；
* 结构化 compatibility findings；
* dynamic difference-edge model；
* Project schema v3；
* offsets/anchors/view state 持久化；
* derived alignment cache；
* project open/save/relink 接入 GUI。

## 阶段 6：高级分析和硬件

完成：

* synchronized zoom；
* ROI；
* threshold mask；
* analysis grid；
* exact plane diff；
* P010；
* 10-bit；
* D3D11VA；
* hardware/performance test suites。

---

# 原始审计的最终判断

当前代码的主干技术路线是正确的，并且已经跨过最危险的架构阶段：多路数据模型、canonical source、持久 actor、独立 analysis service、原子 FrameSet、GPU renderer 和 presentation acknowledgement 都已成立。

目前最大的不足已经不是“还缺一个按钮”，而是以下四个工程闭环：

1. **缓存闭环**：现有 cache key 和缺少真实 Prefetch，使 actor 并行优势没有完全转化为交互速度。
2. **媒体运行时闭环**：PTS index、SwsContext、NV12 buffer 和 signature 仍存在重复构建与拷贝。
3. **对齐可信度闭环**：当前算法适合小偏移与少量掉帧，但还不适合持续漂移、跨帧率和复杂重复场景。
4. **产品状态闭环**：UI 仍硬编码 A/B/C，项目持久化未接入，对齐语义没有完整保存。

因此，下一步最值得投入的路线不是继续扩大功能面，而是依次完成：

> **CI/QML 封板 → 解码缓存与预取 → 专用 signature 分析链 → 时间引导和分段对齐 → 动态 UI/项目持久化 → D3D11VA/P010。**

这条顺序可以最大程度降低后续硬件化时的返工风险。
