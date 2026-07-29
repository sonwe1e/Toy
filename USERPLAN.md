# 核心结论

这笔提交不是简单补功能，而是已经完成了从“双视频播放器”向“2～3 路 VFI 专用比较器”的核心架构迁移：`FramePair` 已升级为 `FrameSet`，三路布局、任意两路误差图、全局偏移估计、带状序列对齐、掉帧/重复帧检测和手动锚点都已有真实实现。

但目前**还不适合直接合并为稳定主线版本**。现在最需要做的不是继续扩充 UI，而是先修复四个会影响正确性的 P0 问题：

1. 非第一路 Reference 的标准时间线处理错误。
2. 普通解码失败会被伪装成 `Missing frame`。
3. 完整序列对齐结果会在播放过程中反复进行 O(N) 快照复制。
4. 误差图一侧缺帧时会退化为显示单路原图，容易造成误判。

此外，当前 `Quality` 工作流已经失败，`Build and Test` 仍处于排队状态，因此分支目前也不满足合并门禁。

按工程成熟度判断：

| 模块           | 当前状态         |
| ------------ | ------------ |
| 2～3 路核心数据模型  | 较完整          |
| 精确逐帧和原子显示    | 较完整          |
| 三路布局和任意两路差分  | 基本完成         |
| 全局偏移和序列对齐    | 算法已实现，工程闭环不足 |
| 长视频性能        | 明显不足         |
| 解码调度         | 仍需重构         |
| 媒体格式兼容性      | 仍处于第一阶段      |
| 产品级错误表达      | 部分情况下会误导     |
| CI、硬件测试和发布门禁 | 未完成          |

---

# 一、本次提交已经完成的关键工作

## 1. 多路数据模型已经成立

新的 `FrameSet` 会在同一个 canonical frame 下携带所有已加载源。缺帧使用显式 `Missing` 条目表示，不允许用邻帧或最后一帧静默替代；同时要求帧句柄与 `sourceFrameId` 必须同时存在或同时缺失。

这一点非常重要，因为它使以下场景都能被统一表达：

* Reference + Prediction 1；
* Prediction 1 + Prediction 2；
* Reference + 两个 Prediction；
* 某一路开头或结尾少一帧；
* 序列对齐检测出中间掉帧；
* 手动锚点导致某个标准帧超出源范围。

## 2. 对齐算法不是占位实现

全局偏移算法使用了：

[
D=0.50D_{\text{SSIM}}+
0.35D_{\text{gradient}}+
0.10D_{\text{pHash}}+
0.05D_{\text{luma}}
]

并从高活动帧中选择证据，使用候选代价中位数，比较最优和次优候选的 margin 来决定是否自动应用。

序列对齐采用带状动态规划，支持：

* 正常匹配；
* Prediction 缺帧；
* Prediction 额外帧；
* 重复帧分类；
* 每帧置信度；
* 整体映射置信度。

空间和计算复杂度被限制为 (O(NW))，而不是完整 (O(N^2))。

手动锚点也已经支持单调验证、区间插值和边界外偏移延伸。

## 3. 三路渲染与误差图已落地

`ComparisonSurface` 已支持：

* Two-up；
* Three-up；
* Reference focus；
* Difference；
* 任意两路差分边；
* RGB、Luma、Chroma、Heatmap；
* 1x～16x gain；
* Nearest、Bilinear、Bicubic。

GPU shader 中也确实实现了 bicubic 采样、独立颜色矩阵变换和四种差分模式，并不是 UI 中有选项但底层没有实现。

## 4. 快速连续逐帧操作得到改善

连续按键会基于“最新请求帧”继续累加，而不是始终基于最后一次已经显示的帧。例如连续按五次下一帧会形成 101、102、103、104、105 的目标链，旧请求可以被后续请求替代。

这比 `video-compare` 的缓存游标式 A/D 语义更符合逐帧审查工作流。

---

# 二、必须优先修复的 P0 问题

## P0-1：Reference 位于 B 或 C 时，provider 仍假设 A 是 canonical source

领域层正确地规定：

* 有 Reference 时，Reference 是 canonical source；
* 没有 Reference 时，第一路是 canonical source。

UI 也允许将 A、B 或 C 任意一路选作 Reference，并保持 A、B、C 原始顺序提交。

协调器构造 `FrameProviderOpenRequest` 时同样保持原始 source order，没有把 Reference 移动到第一个位置。

但 `MultiSourceFrameProvider::executeOpen()` 使用：

```cpp
const MediaDescriptor& canonicalDescriptor =
    request.sources.front().descriptor;
```

并用第一路描述符验证 canonical timeline。

因此会出现：

* A=30 FPS、B=24 FPS、Reference=B：领域验证允许打开并产生 warning，但 provider 会错误地拿 A 的 30 FPS 验证 B 的 24 FPS timeline。
* Reference=C 且 C 为 VFR：provider 会拿 A 的 CFR/VFR 属性做判断。
* Reference 不是第一路时，canonical 规则在 domain 和 adapter 中不一致。

### 实现方案

修改 `FrameProviderOpenRequest`：

```cpp
struct FrameProviderOpenRequest {
    PlaybackRequestContext context;
    std::vector<domain::ComparisonSource> sources;
    domain::SourceId canonicalSourceId;
    domain::CanonicalTimeline timeline;
};
```

provider 必须显式查找：

```cpp
const auto canonical = std::find_if(
    request.sources.begin(),
    request.sources.end(),
    [&](const auto& source) {
        return source.id == request.canonicalSourceId;
    });

if (canonical == request.sources.end()) {
    return invalidArgument;
}
```

所有涉及 canonical slot 的代码都必须使用 `canonicalSourceId`，禁止继续依赖 `front()`。

必须增加三组测试：

1. Reference=B，A/B 帧率不同。
2. Reference=C，A/B/C 原始顺序不变。
3. Reference=B 且 B 为 VFR，确认运行时 timeline 属于 B。

---

## P0-2：普通解码错误会被伪装成 `Missing`

当前 provider 收集各路 future 结果时，只有 `kFrameBudgetExceeded` 会让整个请求失败；其余任何解码错误都会被转换成一个 `Missing` 条目。

这意味着以下错误可能被错误地呈现为“这一帧缺失”：

* 文件读取错误；
* 源文件在 probe 后发生变化；
* codec 解码失败；
* PTS 索引与真实帧不一致；
* unsupported pixel format；
* FFmpeg seek 失败；
* 视频损坏。

这对模型评测工具是危险的，因为用户会把“解码器出错”理解成“预测结果少了一帧”。

### 实现方案

`Missing` 只能由映射层产生，不能由 decoder 产生。

建议增加：

```cpp
enum class MissingReason {
    AlignmentGap,
    BeforeSourceStart,
    AfterSourceEnd,
};

struct MappedSourceFrame {
    SourceId sourceId;
    optional<FrameId> sourceFrameId;
    optional<FrameHandle> frame;
    FrameMatchKind matchKind;
    optional<MissingReason> missingReason;
};
```

处理规则：

```cpp
if (mappingExplicitlyMissing || mappedFrameOutOfRange) {
    appendMissing(...);
} else {
    auto decoded = decoder.decode(...);
    if (!decoded) {
        failWholeFrameRequest(decoded.error());
        return;
    }
}
```

可以保留上一张已经显示的 `FrameSet`，并在 UI 顶部显示 source-specific error，但绝不能发布一个伪造的 `Missing` 新帧。

---

## P0-3：完整序列映射被放进 Snapshot，播放时存在 O(N) 复制

`SessionSnapshot` 直接包含：

```cpp
std::vector<SequenceAlignmentResult> sequenceAlignments;
```

每个 `SequenceAlignmentResult` 又可能携带数万乃至十万条逐帧映射。

协调器每次发布状态时会执行：

```cpp
std::make_shared<const SessionSnapshot>(state_);
```

也就是完整复制 `state_`。

序列分析完成后，每次显示一帧、播放状态变化或错误变化，都可能复制完整对齐表。对于两条 50,000 帧映射，这会产生明显的 CPU 消耗、内存分配和 UI 抖动。

### 实现方案

把重型分析数据移出高频 Snapshot：

```cpp
struct AlignmentReport {
    uint64_t revision;
    vector<SequenceAlignmentResult> results;
    vector<TimelineMarker> markers;
};

struct SessionSnapshot {
    ...
    shared_ptr<const AlignmentSummary> alignmentSummary;
    uint64_t alignmentRevision;
};
```

建议进一步拆分：

* `sequenceAlignmentMaps_`：协调器内部使用的不可变映射。
* `AlignmentSummary`：每路置信度、异常数量、是否自动应用。
* `TimelineMarkerIndex`：供 UI 按可视时间范围查询。
* 完整 mapping：不进入 16 ms UI 投影路径。

UI 只查询当前时间线窗口附近的 marker，例如：

```cpp
markersInRange(firstFrame, lastFrame, maximumCount);
```

---

## P0-4：差分边缺帧时会显示单路原图

差分模式下，如果所选的两路中只有一路存在，渲染器会直接显示存在的那一路原图；如果两路都不存在，则再寻找其他可用源显示。

这会出现如下误导：

* UI 当前显示为 `Difference: Reference ↔ Prediction 2`；
* Prediction 2 实际 Missing；
* 画面却显示完整 Reference；
* 用户可能认为“误差图非常亮”或误以为已经切回普通画面。

### 实现方案

Difference 模式必须有明确的可用性状态：

```cpp
enum class DifferenceAvailability {
    Available,
    FirstSourceMissing,
    SecondSourceMissing,
    BothMissing,
};
```

缺帧时：

* 不运行 Difference shader；
* 保持黑色或棋盘格背景；
* 绘制明显的 `Difference unavailable`；
* 标注具体缺少哪一路和对应 canonical frame；
* 不允许退化为单路原图。

普通 Three-up 视图中的 Missing panel 也不应只是黑屏，应增加 `Missing` 标签或斜线纹理。

---

## P0-5：CI 当前不是绿色

当前分支的实际工作流状态为：

* `Quality`：失败；
* `Build and Test`：排队。

`Quality` 会执行仓库指南检查、coverage gate、全仓库 format-check 和 lint。

目前连接器没有提供失败日志，所以不能准确断言失败发生在 format、lint 还是前置脚本。但在合并前必须做到：

```text
Quality             PASS
Build and Test      PASS
Release tests       PASS
Package smoke       PASS
```

不能只依赖本地测试通过的描述。

---

# 三、P1：性能和工程架构仍需重构

## 1. 当前不是真正的“每路一个 Decode Actor”

代码注释称每一路使用独立 actor，但实际每次请求一帧时，都会为 2～3 路调用：

```cpp
std::async(std::launch::async, ...)
```

然后等待所有 future。

这可能带来：

* 反复创建异步任务或线程；
* 调度延迟不稳定；
* 三路 60 FPS 下大量线程生命周期成本；
* 无法为每一路建立独立优先级和背压；
* 一个慢源会阻塞 provider 总 worker。

### 目标架构

```text
ComparisonCoordinator
        │
MultiSourceFrameProvider
        │
FrameSetAssembler
   ┌────┼────┐
   │    │    │
Actor A Actor B Actor C
```

每个 `SourceDecodeActor`：

* 拥有一个长期 worker thread；
* 独占一个 `SoftwareDecoder` 或硬件 decoder context；
* 有 bounded Exact、Sequential、Prefetch mailbox；
* Exact 可抢占 Prefetch；
* 使用 session epoch、generation、request ID 丢弃过期结果；
* 返回 `SourceFrameCompleted`；
* `FrameSetAssembler` 只在所有 source slot 终止后组装。

需要增加的性能测试：

* 连续播放过程中线程数量不增长；
* 快速连续 seek 不产生线程泄漏；
* 三路 exact seek 的延迟接近 `max(TA, TB, TC)`；
* 任何时刻不发布部分集合。

---

## 2. “Find drops” 会独占整个播放后端

序列分析目前在 provider 的同一个 worker 中：

1. 完整解码 canonical source；
2. 完整解码 Prediction 1；
3. 完整解码 Prediction 2；
4. 运行 DP；
5. 最后一次性返回结果。

每路最多允许 50,000 帧，内部硬上限为 100,000 帧。

期间：

* 没有进度事件；
* 没有单独的 Cancel command；
* 没有分析超时；
* 播放 provider 被占用；
* 不能边分析边继续审查视频；
* 每次分析都重新解码完整视频。

### 实现方案

新增独立服务：

```cpp
class IAlignmentAnalysisService {
    submit(GlobalOffsetJob);
    submit(SequenceAlignmentJob);
    cancel(AnalysisJobId);
};
```

事件：

```cpp
AlignmentAnalysisStarted
AlignmentAnalysisProgress
AlignmentAnalysisCompleted
AlignmentAnalysisCanceled
AlignmentAnalysisFailed
```

分析任务使用独立低优先级 worker pool，不得占用播放 actor。

同时增加 signature cache：

```text
cache key =
source fingerprint
+ FFmpeg version
+ signature algorithm version
+ downsample configuration
```

缓存内容可以是每帧 16×9 或后续多尺度 signature。这样第二次运行序列分析不必重新完整解码。

---

## 3. Probe 后 Decoder 又重新建立一次完整 PTS 索引

`MediaProbe` 已经完成显示顺序时间戳扫描；但每个 `SoftwareDecoder::open()` 又调用一次 `buildPresentationTimestampIndex()`。

三路长视频相当于：

* Probe 扫描三次；
* Decoder open 再扫描三次；
* Sequence analysis 又完整解码三次。

### 实现方案

probe 应输出一个 adapter-internal runtime source：

```cpp
struct ProbedMediaRuntime {
    MediaDescriptor descriptor;
    shared_ptr<const NativeTimelineIndex> nativeTimeline;
};
```

`NativeTimelineIndex` 不进入 domain 和项目 JSON，但可以在 `media_ffmpeg` 内部通过 source ID/session registry 共享给 decoder。

还应提供基于 fingerprint 的磁盘缓存，避免每次启动都重新扫描。

---

## 4. 每帧重新创建 SwsContext 和 CPU 缓冲

当前每次成功解码一帧都会：

* 调用 `sws_getContext()`；
* 创建一个新的 NV12 `std::vector`；
* 完整转换；
* 再复制到 FrameResource。

三路 1080p60 或 4K 下，这会是明显瓶颈。

### 改造顺序

第一步：

* 使用 `sws_getCachedContext()`；
* 为每个 decoder 持久保存转换 context；
* 使用 FrameResourceFactory 的可复用 buffer pool；
* 避免中间 `std::vector`。

第二步：

* decoder 直接写入预分配 NV12 resource。

第三步：

* D3D11VA 解码后直接将 NV12/P010 GPU texture 交给 renderer；
* 避免 GPU→CPU→GPU。

---

# 四、对齐算法还需要的可靠性增强

## 1. 16×9 signature 对复杂内容过于粗糙

当前 signature 只有 144 个亮度值。

它对以下场景可能不稳定：

* 大面积静止游戏画面；
* 重复 UI 或相似菜单；
* 淡入淡出；
* 黑场；
* 周期性动画；
* 小面积高速运动；
* 两个插帧结果仅局部区域不同。

现有测试主要使用规则生成的 synthetic pattern，虽然覆盖了固定偏移、缺帧、重复和纯常量歧义，但还没有真实视频中的场景切换、局部运动和重复场景测试。

### 建议升级

使用两级 signature：

```text
Level 1: 16×9，快速全局搜索
Level 2: 64×36，候选验证
```

Level 2 可额外包含：

* Sobel magnitude；
* 局部方差；
* edge histogram；
* 可选的轻量 DINO/ConvNeXt embedding 离线模式。

不需要立即引入重型神经网络；先做多尺度亮度和边缘即可显著改善。

## 2. 当前自动应用只看整体平均置信度

序列结果是否自动应用取决于：

* 平均匹配代价；
* 平均 confidence。

因此可能发生：

* 90% 区域非常可靠；
* 10% 区域高度歧义；
* 整体平均仍达标；
* 整张 mapping 被自动应用。

### 实现方案

采用分段状态：

```cpp
enum class AlignmentSegmentState {
    Accepted,
    ReviewRequired,
    Rejected,
};
```

自动应用条件应同时考虑：

* mean cost；
* mean confidence；
* P10 confidence；
* 最长低置信度连续区间；
* scene cut 附近置信度；
* anomaly density。

低置信度区间应保留 strict/global-offset 映射，等待用户确认，而不是整张表全部自动应用。

## 3. 手动锚点没有参与 DP 约束

当前手动锚点只在最终 `sourceMappingsFor()` 中覆盖 sequence map；运行 sequence analysis 时，锚点并未作为边界条件输入 DP。

正确做法是：

* 将锚点划分为多个区间；
* 每个区间在两个锚点之间单独执行带状 DP；
* 锚点位置作为硬约束；
* 锚点外使用延伸 offset；
* 若锚点与自动证据冲突，明确报告冲突。

## 4. 缺少结果确认、撤销和版本管理

目前高置信度 global estimate 和 sequence map 会自动应用。

还需要：

* `Apply suggested alignment`；
* `Undo alignment`；
* `Return to strict index`；
* 删除单个 anchor；
* 清除某一路 anchor，而不只是全部清除；
* 比较自动应用前后的映射变化；
* 保留 mapping revision。

---

# 五、产品与持久化仍欠缺的内容

## 1. 项目文件没有保存对齐状态

Schema v2 当前保存：

* 2～3 路源；
* Reference；
* marks；
* last displayed frame；
* workspace state。

没有正式定义：

* global offsets；
* approved sequence map；
* manual anchors；
* alignment mode；
* alignment algorithm version。

`Project` domain 中也没有对应字段。

### 建议

不要直接把数万帧 mapping 塞入项目 JSON。推荐：

```json
{
  "alignment": {
    "mode": "manual-anchors",
    "offsets": {
      "1": 1,
      "2": -1
    },
    "anchors": {
      "1": [
        { "canonicalFrame": 824, "sourceFrame": 825 }
      ]
    },
    "derivedMapCacheKey": "..."
  }
}
```

* 手动 offset 和 anchors 属于用户语义，应持久化。
* 完整 sequence map 是派生数据，可单独缓存。
* 缓存必须绑定所有 source fingerprints 和算法版本。

## 2. CompatibilityReport 丢失了 source pair 信息

领域中的 `CompatibilityFinding` 包含具体 source IDs 和 severity。

但 Snapshot 只保留 `MediaErrorCode` 列表，UI 无法知道：

* 哪两路帧率不一致；
* 哪两路发生颜色元数据差异；
* 同一个 warning 是否出现多次。

此外，`kAlignmentRequired` 已定义，但当前 validator 把帧数、帧率、时长不一致全部标记为普通 `Warning`。

建议：

* 帧数、帧率、时长差异改为 `AlignmentRequired`；
* 分辨率、颜色差异保留 `Warning`；
* Snapshot 保存结构化 `CompatibilityFindingSummary`；
* UI 显示具体来源，例如 `A ↔ C: frame count mismatch`。

## 3. UI 仍然硬编码 A/B/C

核心已经是动态的 2～3 路 collection，但 Controller 仍使用：

* `sourceAFilename/sourceBFilename/sourceCFilename`；
* 三个 error key；
* 三个 qint64 offset 参数；
* raw source index。

短期支持最多三路时这可以运行，但后续维护成本较高。建议改为：

```cpp
Q_PROPERTY(QAbstractItemModel* sources READ sources CONSTANT)
```

Source model 每一行包含：

```text
sourceId
role
filename
errorKey
frameId
matchKind
confidence
manualOffset
missing
```

这样 QML 用 Repeater 构建 source card、label 和 offset control。

---

# 六、仍未完成的分析和媒体能力

当前项目文档也明确记录了后续媒体阶段尚未完成：

* 10-bit/P010；
* YUV422/YUV444；
* BGRA/RGBA；
* rotation 和 SAR；
* D3D11VA；
* 软件回退状态；
* optional compatibility proxy。

目前实际支持仍限定于：

* H.264、HEVC、MPEG-4 Part 2；
* 8-bit 4:2:0；
* SDR；
* BT.601/BT.709；
* 软件解码。

硬件性能 workflow 虽然已经建立，但文档明确说明真正的 hardware/performance suites 尚未落地，当前会报告零匹配测试。

分析 UI 还应继续增加：

* 差分阈值 mask；
* ROI 放大和同步 pan/zoom；
* 1:1 pixel 模式；
* Difference unavailable 占位；
* 完整 analysis grid；
* pixel-exact / resampled 明确状态；
* 低置信度区间而非单点 marker。

---

# 七、推荐的具体实施路线

## 阶段一：正确性封板

优先修改：

```text
Ports.h
PlaybackCoordinator.cpp
MultiSourceFrameProvider.cpp
FrameSet.h
D3d11ComparisonRenderer.cpp
SessionSnapshot.h
```

完成：

1. `FrameProviderOpenRequest` 增加 `canonicalSourceId`。
2. decoder error 不再转换成 Missing。
3. Missing 增加明确 reason。
4. Difference 缺帧不再显示单路原图。
5. 重型 alignment map 移出 Snapshot。
6. 修复 Quality workflow 并保证所有 PR checks 绿色。

这一阶段完成前不建议继续增加媒体格式。

## 阶段二：持久化 Decode Actor

新增：

```text
SourceDecodeActor.h/.cpp
FrameSetAssembler.h/.cpp
DecodeMailbox.h/.cpp
```

删除逐帧 `std::async`。

每一路一个持久 actor，provider 只负责：

* 路由请求；
* 控制 generation；
* 汇合结果；
* 发布完整 FrameSet。

## 阶段三：独立 Alignment Analysis Service

新增：

```text
alignment/
├── AlignmentAnalysisService
├── SignatureExtractor
├── SignatureCache
├── GlobalOffsetAnalyzer
├── SequenceAlignmentAnalyzer
└── AlignmentProgress
```

支持：

* 进度；
* 取消；
* 缓存；
* 后台低优先级；
* 不阻塞播放；
* 分段置信度；
* anchors 约束 DP；
* 分析版本和结果 revision。

## 阶段四：完善分析 UI

依次完成：

1. Missing panel 和 Difference unavailable。
2. 同步缩放和平移。
3. ROI 放大。
4. 阈值 mask。
5. pixel-exact 状态。
6. Analysis grid。
7. Alignment 审核、确认、撤销、单个 anchor 删除。

## 阶段五：媒体和性能

按以下顺序推进：

1. 共享 PTS index，删除重复扫描。
2. 缓存 SwsContext。
3. FrameResource buffer pool。
4. rotation 和 SAR。
5. P010/10-bit。
6. D3D11VA。
7. GPU 纹理直通 renderer。
8. optional compatibility proxy。

## 阶段六：持久化和发布门禁

* 项目文件保存 offsets 和 anchors；
* 派生 sequence map 使用独立缓存；
* source fingerprint 变化后自动使缓存失效；
* 真实三路 1080p60 测试；
* 4K 三路逐帧 seek 测试；
* 长视频索引和 sequence analysis 性能测试；
* Debug、Release、Package、Quality 全部绿色。

---

# 最终验收标准

下一阶段完成的判断不应是“UI 中已经有按钮”，而应满足以下可测试条件：

1. Reference 选 A、B、C 都能正确建立 canonical timeline。
2. 损坏视频或 decoder 错误绝不会被显示为 `Missing frame`。
3. 序列映射有 100,000 条时，逐帧播放不会复制整个映射表。
4. 连续播放过程中线程数量保持稳定，不因每帧创建 `std::async` 任务而增长。
5. `Find drops` 有进度、有取消，并且不阻塞正常播放。
6. 差分边任一路 Missing 时，显示明确的不可计算状态。
7. 自动对齐的局部低置信度区间不会被静默自动应用。
8. 对齐 offset 和手动 anchors 在重启项目后可恢复。
9. 三路 1080p60 的原子 FrameSet 不出现单路提前更新。
10. GitHub `Quality`、`Build and Test`、Release 和 package smoke 全部通过。

---

**综合判断：这笔提交已经完成了最难的“架构方向和基础功能迁移”，但当前仍属于功能可演示、正确性尚需封板、性能尚未工程化的阶段。最优下一步是暂停新增功能，先完成 canonical source、错误语义、Snapshot 体积和差分缺帧四个 P0 修复，再进行 Decode Actor 与独立 Alignment Service 重构。**
