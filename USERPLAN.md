



# 核心结论

这个项目最终不应该是“三个播放器项目合并后继续共存”，而应该是：

> **以 `DualVideoStudio` 为唯一技术主干，彻底淘汰 `DualVideoTool` 的播放链路，把 `video-compare` 仅作为交互与算法参考，重构成一个原生支持 2～3 路视频、显式对齐、逐帧精确导航和任意两路误差图的 VFI 专用比较器。**

最终产品的核心对象也不应继续叫 `FramePair`，而应升级为 **`FrameSet`**：在同一个标准帧位置上，原子地包含 Reference、Prediction 1、Prediction 2 三路画面，以及三组可选差分关系。

这条路线既能保留 `DualVideoStudio` 现有最有价值的精确帧、异步状态机、GPU 渲染和测试体系，也能避免把 `DualVideoTool` 的代理依赖、QImage 拷贝、剪辑导出，以及 `video-compare` 的 SDL 巨型状态机、示波器、裁剪、HDR、滤镜等不必要功能带入最终工程。

---

# 一、仓库现状与审计边界

当前 GitHub 中可以检索到的核心代码位于导入提交 `6d3b3e9bf973def125eea26f8142990943e8b981`，提交内容为 `DualVideoStudio`、`DualVideoTool` 和 `video-compare` 三个工作区的整体导入。该提交目前没有关联的 GitHub Actions 运行记录。fileciteturn4file0L1-L2 fileciteturn49file0L1-L1

默认分支目前没有正常暴露这次导入内容，因此本次审计采用：

- `DualVideoStudio`、`DualVideoTool`：直接读取导入提交中的实际代码。
- `video-compare`：根据目录名、A/D 操作方式和功能特征，结合可读取的 `pixop/video-compare` 上游代码进行分析。它与用户描述的项目高度吻合，但在默认分支修复前，不能保证仓库内副本与上游当前版本逐行一致。

这也是合并工作的第一个阻塞项：**先让导入提交进入一个可构建、可发起 PR、可运行 CI 的正常分支，再开始重构。**

---

# 二、三个项目分别应该如何处理

| 工程 | 当前优势 | 主要问题 | 最终处理 |
|---|---|---|---|
| `DualVideoStudio` | 原子 A/B 帧、精确帧序号、PTS 索引、VFR 时间线、异步请求身份、取消与过期结果过滤、D3D11 差分渲染、分层测试 | 全部模型硬编码为 A/B；编解码支持范围窄；当前解码仍是软件且 A/B 串行；工程范围包含大量当前不需要的项目、代理、导出能力 | **唯一主干，进行泛化和裁剪** |
| `DualVideoTool` | 已经验证过基本工作流；传统 Qt Widgets 容易快速开发；已有代理、时间线、导出界面 | 播放必须等待代理；QImage CPU 路径；浮点 FPS 映射；职责集中；存在未进入构建的死代码；剪辑与导出不是当前目标 | **淘汰，仅保留少量 UI 经验和测试媒体** |
| `video-compare` | FFmpeg 格式适应性较广；时间偏移、缩放、滑动对比、差分模式成熟；交互经过长期使用 | A/D 不是精确逐帧；多路右视频只能切换，不能三路同屏；SDL 状态机庞大；功能远超需求；数据模型仍是 Left/Right | **作为参考实现，不作为合并底座** |

`DualVideoTool` 自己的后续架构设计文档已经明确指出，它不适合作为下一版的结构基础：`PlaybackEngine` 同时承担播放、代理、线程、剪辑和诊断，播放被完整代理生成阻塞，帧路径中存在 UI 线程 QImage 拷贝，部分异步操作缺少严格的请求身份。fileciteturn32file0L9-L13

`DualVideoStudio` 的架构方向则是正确的：业务规则、应用协调器、FFmpeg、Windows/D3D11 和 QML 已经分层；渲染发布要求 A/B 属于同一个标准 `FrameId`，并且只有完整帧对可以提交。fileciteturn23file0L3-L9

---

# 三、当前最关键的技术问题

## 3.1 `video-compare` 中 A/D“没有响应”的真实原因

这大概率不是简单的键盘事件失效，而是 **A/D 的语义与用户理解不一致**。

在上游实现中：

- 普通 `A` 增加 `frame_buffer_offset_delta_`，向已经缓存的旧帧移动。
- 普通 `D` 减少该偏移，向缓存中的新帧移动。
- `Shift+D` 才会请求解码下一帧。
- `Shift+A` 才会通过一次反向 seek 尝试得到前一帧。fileciteturn13file0L239-L252

内部又会把缓存偏移强制限制在：

\[
0 \leq offset \leq \min(N_\text{left}, N_\text{right})-1
\]

因此在最常见的情况下：

- 当前处于最新缓存帧时，`offset=0`。
- 按 `D` 后得到 `-1`，随即被截断回 `0`，视觉上完全无变化。
- 播放缓存尚未积累时，按 `A` 也没有更旧帧可进入。
- 左右缓存深度不同时，只能浏览两边都存在的公共缓存区域。fileciteturn21file0L162-L197

所以最终项目中不应继续沿用这一语义。建议：

- `A` / `Left`：标准帧位置减一。
- `D` / `Right`：标准帧位置加一。
- 每次操作都进入 `ComparisonCoordinator::seekFrame()`。
- 正在播放时，第一次逐帧操作先暂停，再跳转。
- “浏览解码缓存历史”不作为用户功能暴露。
- 连续快速按键不应因 `busy` 被丢弃，而应累积为一个最新目标帧，并取消过期解码请求。

这将从根本上解决“按了但没有反应”的体验问题。

---

## 3.2 当前 `DualVideoStudio` 仍然是彻底硬编码的双路系统

现有 `FramePair` 的创建接口必须同时接收 `frameA` 和 `frameB`，内部字段也是固定的 A/B；它从类型层面保证不会产生缺失一侧的可渲染帧对，这一点很好，但无法自然扩展到三路。fileciteturn25file0L12-L52

同样的硬编码还存在于：

- `ValidatedSourcePair` 和 `SourcePairValidator`；
- `SourceRole::kA / kB / kPair`；
- `ReviewController::openPair(A, B)`；
- QML 中的 `selectedSourceA`、`selectedSourceB`；
- `DualVideoSurface` 的两个纹理和 `ReferenceA/ReferenceB`；
- `DirectFrameProvider` 中的 `decoderA_`、`decoderB_`；
- 错误状态中的 `sourceAErrorKey`、`sourceBErrorKey`。fileciteturn26file0L11-L31 fileciteturn38file0L21-L58 fileciteturn46file0L20-L72

因此，第三路不能通过继续增加 `C`、`decoderC_`、`sourceCErrorKey` 来实现。那会让所有模块形成三路硬编码，未来任何扩展都会再次重写。

正确做法是一次性将系统泛化成 **2～3 路动态源集合**。

---

## 3.3 “是否允许帧率、帧数、时长不一致”目前自相矛盾

旧设计文档规定两路必须：

- 有相同的有理数帧率；
- 有相同帧数；
- 持续时间误差不超过一帧。fileciteturn32file0L89-L101

但当前真实代码中的 `SourcePairValidator` 只检查两个描述符是否有效，以及帧数是否完全一致；它并不比较帧率或时长。fileciteturn27file0L33-L60

单元测试甚至明确验证了：

- 不同帧率可以通过；
- 不同时长可以通过；
- 两个 VFR 视频只要索引帧数相同也可以通过。fileciteturn29file0L61-L100

与此同时，界面仍然向用户显示“必须相同帧率”“必须相同时长”的错误信息。fileciteturn35file0L128-L175

这不是小型文档问题，而是会直接导致错误播放映射的契约问题。建议彻底改成：

> **单个视频是否合法由 `SourceValidator` 决定；多个视频是否完全兼容不再决定能否打开，而是生成一份 `CompatibilityReport`。**

兼容性问题应该分级：

- **Fatal**：无法打开、无法建立显示顺序索引、解码器不可用。
- **Warning**：帧数不同、帧率不同、时长不同、分辨率不同、颜色元数据不同。
- **Alignment required**：存在全局偏移、掉帧、重复帧或时间漂移。

这样，帧数差一两帧的视频仍然可以进入播放器，只是时间线和缺失帧会被明确标识。

---

## 3.4 当前编解码兼容性还不足以覆盖用户描述的输入

现有 `MediaProbe` 已经有一个很强的能力：它会构建显示顺序的 PTS 索引，并根据真实时间戳判断 CFR 或 VFR；VFR 视频会形成独立的 `FrameTimeline`，而不再依赖浮点 FPS。fileciteturn42file0L5-L94

但当前输入限制仍然很窄：

- 只允许 H.264、H.265/HEVC 和 MPEG-4 Part 2；
- 只允许 8-bit 4:2:0；
- 只接受 SDR BT.601/BT.709；
- HDR 会被拒绝；
- `d3d11VaDecode` 当前标记为 `false`；
- 解码链实际使用的是 `SoftwareDecoder`。fileciteturn41file0L158-L207 fileciteturn41file0L304-L331 fileciteturn42file0L205-L225

因此，“编码格式不固定”不能仅靠增加文件扩展名过滤器解决。需要把能力拆成三层：

1. **容器和解码器能力**：FFmpeg 是否可以解码。
2. **像素格式归一化能力**：是否可以转换成内部 NV12、P010 或统一的分析格式。
3. **渲染与差分能力**：颜色空间、位深和旋转是否能够被正确解释。

不建议继续使用固定 codec 白名单作为主要策略，而应形成一张能力矩阵；无法直接进入 GPU 路径的输入，再使用可选的“兼容性代理”，而不是让所有视频都必须先转码。

---

## 3.5 当前双路解码链不能直接扩展到高性能三路

`DirectFrameProvider` 虽然有明确的 Exact、Sequential 和 Prefetch 优先级以及取消机制，但只有一个 provider worker，并且会先解码 A，再解码 B。fileciteturn43file0L31-L35 fileciteturn45file0L51-L81

扩展第三路后，如果继续串行执行：

\[
T_\text{FrameSet}=T_A+T_B+T_C
\]

逐帧响应和连续播放延迟都会显著上升。

应改成：

\[
T_\text{FrameSet}\approx \max(T_A,T_B,T_C)+T_\text{assemble}
\]

即每个输入拥有独立的 `SourceDecodeActor`，协调器并行发送请求，`FrameSetAssembler` 等待同一请求身份的结果。任何过期结果都丢弃，任何一侧缺失都形成显式的 `MissingFrame`，而不是悄悄复用邻帧。

---

# 四、建议明确的产品模型

## 4.1 两种比较模式

为了避免自动对齐掩盖推理错误，播放器应明确提供两种模式。

### Strict Index Mode：默认用于插帧结果检查

标准帧 \(i\) 直接映射到所有输入的第 \(i\) 帧：

\[
M_s(i)=i
\]

若某一路不存在这一帧，则该面板显示 `Missing frame`。不自动补帧、不重复最后一帧、不静默裁掉多余帧。

这最适合 Reference 与模型预测结果，因为帧数不一致本身往往就是需要被发现的推理问题。

### Aligned Capture Mode：用于手机录屏、二次组合和有偏移的视频

允许：

- 全局帧偏移；
- 开头或结尾多一两帧；
- 中间掉帧或重复帧；
- VFR；
- 手工锚点和局部漂移修正。

所有自动映射都必须显示当前状态，例如：

> `Prediction 2: Auto-aligned, offset +1, one missing frame at 824`

不能让用户误以为仍然是严格同帧比较。

---

## 4.2 新的数据模型

建议把当前模型重构为：

```cpp
using SourceId = std::uint32_t;

enum class ComparisonRole {
    Reference,
    Prediction,
};

enum class FrameMatchKind {
    ExactIndex,
    GlobalOffset,
    AutoAligned,
    ManualAnchor,
    Missing,
};

struct ComparisonSource {
    SourceId id;
    ComparisonRole role;
    MediaDescriptor descriptor;
    std::string displayName;
};

struct MappedSourceFrame {
    SourceId sourceId;
    std::optional<FrameId> sourceFrameId;
    std::optional<FrameHandle> frame;
    MediaTime presentationTime;
    FrameMatchKind matchKind;
    float alignmentConfidence;
};

struct FrameSet {
    FrameId canonicalFrameId;
    MediaTime canonicalTime;
    std::vector<MappedSourceFrame> sources;  // size 2 or 3
};

struct DifferenceEdge {
    SourceId first;
    SourceId second;
};
```

关键约束是：

> 一个 `FrameSet` 必须为每个已加载源包含一个条目，但条目可以明确表示 `Missing`。

这样既保留了原子提交，又能正确处理某一路少一帧，而不需要拒绝整个会话。

---

## 4.3 Reference 不应是固定的 Source A

会话模型应为：

```cpp
struct ComparisonSession {
    std::vector<ComparisonSource> sources;  // 2..3
    std::optional<SourceId> referenceSource;
    SourceId canonicalSource;
    AlignmentPolicy alignmentPolicy;
};
```

规则如下：

- Reference + 两个预测：Reference 是标准时间线。
- Reference + 一个预测：Reference 是标准时间线。
- 只有两个预测：第一个输入默认作为标准时间线，但可以切换。
- Reference 可在 UI 中重新指定，不必重新打开解码器。
- Error map 由 `DifferenceEdge` 选择，而不是写死 A-B。

---

# 五、时间对齐的具体技术路线

## 5.1 第一级：元数据与严格帧序号检查

打开视频时立即生成：

- 实际显示顺序帧数；
- 每帧 PTS 索引；
- CFR/VFR 分类；
- 起始时间；
- 帧率候选；
- 分辨率、旋转、SAR、颜色范围和位深。

`DualVideoStudio` 已经能够构建显示顺序 PTS 索引，并将 VFR 时间线归一化到第一帧，这部分可以直接保留。fileciteturn42file0L96-L153

不过当前实现会在 probe 阶段完整扫描时间戳索引。三条长视频会使首次打开时间近似变成 O(总帧数)。建议改成：

- UI 先完成快速 metadata probe；
- CFR 且元数据可信时，允许尽快显示第一帧；
- 后台建立完整索引；
- VFR、帧数未知或时间戳异常时，完整索引成为强制步骤；
- 索引进度应显示在每一路源卡片上。

---

## 5.2 第二级：全局偏移估计

对于帧数只差一两帧或整体错位的情况，先估计全局偏移：

\[
E(\delta)=\operatorname{median}_{i\in S}
D\left(R_i,\;P_{i+\delta}\right)
\]

其中：

- \(\delta\) 在小范围内搜索，例如 \([-16,16]\)；
- \(S\) 选择若干运动明显的采样帧；
- 图像先缩放到低分辨率亮度图；
- 距离函数使用结构和边缘，而不是纯像素差：

\[
D=\alpha(1-\mathrm{SSIM})
+\beta\left\|\nabla Y_R-\nabla Y_P\right\|_1
+\gamma D_{\mathrm{pHash}}
\]

使用中位数而不是均值，可以避免个别插帧坏帧或场景切换主导结果。

只有最佳偏移与次优偏移之间的差距足够明显时才自动应用；否则应提示用户手动选择。

---

## 5.3 第三级：掉帧和重复帧检测

若单一偏移无法解释整个视频，则使用带状动态规划建立单调映射。

状态：

\[
C(i,j)
\]

表示 Reference 前 \(i\) 帧与目标视频前 \(j\) 帧的最小累计代价。转移包括：

- 匹配：\((i-1,j-1)\)
- Reference 缺帧：\((i-1,j)\)
- Prediction 缺帧或重复：\((i,j-1)\)

因为实际问题通常只差一两帧，不需要完整 \(O(N^2)\) DTW。将搜索限制在当前估计偏移附近宽度 \(W\) 的带状区域：

\[
O(NW),\qquad W\approx 8\sim16
\]

即可处理：

- 中间少一帧；
- 某帧被重复两次；
- 开头或结尾多一帧；
- 小范围时间漂移。

生成的 `AlignmentMap` 应明确记录每个标准帧映射到哪一帧，以及缺失和重复的位置。

---

## 5.4 手动锚点

自动对齐必须允许用户覆盖：

```text
Reference frame 824  <->  Prediction 2 frame 825
Reference frame 1900 <->  Prediction 2 frame 1902
```

多个锚点之间使用分段单调映射。时间线上显示：

- 全局偏移；
- 掉帧点；
- 重复帧点；
- 手动锚点；
- 低置信度区域。

这比单纯提供一个 `+/- time shift` 更适合模型结果检查。

---

# 六、播放、逐帧和渲染架构

建议的主链路是：

```text
QML
  │
ReviewController
  │
ComparisonCoordinator
  ├── AlignmentService
  ├── SourceDecodeActor[Reference]
  ├── SourceDecodeActor[Prediction1]
  ├── SourceDecodeActor[Prediction2]
  │
FrameSetAssembler
  │
FrameMailbox
  │
ComparisonSurface / D3D11
  │
PresentationAck
```

## 6.1 原子播放

每个标准帧只发布一次 `FrameSet`：

- 三路都存在：发布三个帧句柄。
- 某一路缺失：发布一个包含 `Missing` 的完整集合。
- 播放跟不上：丢弃整个旧 `FrameSet`，不能只推进其中一路。
- 帧被真正渲染并收到 acknowledgement 后，才更新 UI 的当前帧序号。

现有 `PlaybackCoordinator` 已经拥有会话 ID、epoch、command ID、请求取消、过期结果过滤、精确帧超时和关键事件队列，这部分值得完整保留。fileciteturn39file0L24-L84 fileciteturn39file0L236-L314

## 6.2 逐帧操作

按键操作不应被 UI 的 `busy` 简单吞掉，而应使用目标帧合并：

```text
当前显示 100
快速按 D 五次
目标依次变成 101, 102, 103, 104, 105
取消 101～104 的过期请求
最终提交 105
```

相邻帧解码应优先使用：

- 当前 GOP 内的顺序解码状态；
- 当前帧前后的小型帧缓存；
- Exact 请求高于播放和预取；
- 最新 Exact 请求抢占旧 Exact 请求。

## 6.3 三路显示布局

推荐只保留四种高价值布局：

1. **Two-up**：两路左右或上下。
2. **Three-up**：Reference、Prediction 1、Prediction 2 三列。
3. **Reference focus**：Reference 大图，两个 Prediction 上下排列。
4. **Analysis grid**：选中的两路加一张差分图，第四格显示放大区域或元数据。

不建议第一版默认显示六格“3 原图 + 3 误差图”，因为 1080p/4K 下单格过小，也会显著增加 shader 工作量。可以作为后续 Matrix 模式。

---

# 七、误差图的实现建议

当前 `DualVideoSurface` 已经支持：

- RGB absolute；
- Luma；
- Chroma；
- Heatmap；
- 1x～16x gain；
- A/B 参考画布；
- Nearest、Bilinear、Bicubic。fileciteturn46file0L27-L72

这些能力应保留，但从固定 A/B 改成选择任意 `DifferenceEdge`：

```text
Reference ↔ Prediction 1
Reference ↔ Prediction 2
Prediction 1 ↔ Prediction 2
```

建议区分两类差分。

## 7.1 视觉差分

用于人眼定位问题，可以允许：

- 统一到选择的参考分辨率；
- RGB absolute；
- Luma；
- Signed heatmap；
- 阈值 mask；
- gain 调节。

改变差分模式只触发 GPU 重绘，不重新解码视频。当前设计已经遵循这一原则。fileciteturn24file0L81-L99

## 7.2 像素精确差分

只有满足以下条件时才称为 pixel-exact：

- 分辨率相同；
- 旋转和显示尺寸相同；
- 颜色空间和范围已归一化；
- 没有空间缩放；
- 当前帧映射不是 `Missing`；
- 当前时间对齐策略满足要求。

只要发生缩放，就必须在界面上明确标记：

> `Resampled comparison — not pixel-exact`

当前 QML 已经有类似提示，可继续使用。fileciteturn36file0L90-L103

---

# 八、编解码和性能路线

## 第一阶段：先保证正确

保留 FFmpeg 软件解码，同时扩展：

- H.264、HEVC、MPEG-4 Part 2；
- 8-bit YUV420；
- CFR/VFR；
- 非零起始 PTS；
- B-frame 显示顺序；
- MP4、MOV、MKV、AVI；
- 旋转和 SAR 元数据；
- 不同分辨率。

## 第二阶段：扩展兼容性

增加统一转换层：

```text
Decoder output
  ├── NV12 8-bit
  ├── P010 10-bit
  ├── YUV422 / YUV444
  └── BGRA/RGBA
          ↓
Normalized GPU frame
```

解码器和渲染器能力应独立判断，而不是因为 decoder 能解码就直接承诺可以比较。

## 第三阶段：硬件解码

每一路拥有独立解码上下文，共享同一个 D3D11 device：

- FFmpeg D3D11VA；
- NV12/P010 纹理直接进入渲染；
- 避免 GPU→CPU→GPU；
- 硬件失败时回退软件；
- 回退状态在 UI 中可见。

当前 manifest 已固定 FFmpeg、Qt Base、Qt Declarative 和 Qt Shader Tools，依赖管理基础是完整的。fileciteturn48file0L3-L39

## 可选兼容性代理

代理不应再是播放前置条件。只在以下情况下生成：

- 原视频无法稳定随机 seek；
- 解码格式无法直接进入渲染链；
- 用户主动选择建立快速预览缓存。

`DualVideoTool` 当前 `isReady()` 强制要求代理就绪，并且通过两次 `QImage.copy()` 拆分合成代理，这条链路应彻底删除。fileciteturn33file0L342-L348 fileciteturn34file0L238-L266

---

# 九、三个文件夹合并为一个的具体方法

## Phase 0：修复仓库和构建入口

先从导入提交创建正常开发分支，再合并到 `main`：

```bash
git switch -c refactor/unified-comparator \
    6d3b3e9bf973def125eea26f8142990943e8b981

git push -u origin refactor/unified-comparator
```

随后处理：

- 把 `DualVideoStudio` 移到仓库根目录；
- 根目录增加 README、架构说明和构建说明；
- 删除 `DualVideoTool/package/DualVideoTool.zip`；
- 删除 `tmp`、诊断输出和构建产物；
- 确认嵌套 `.git.bak` 不被提交；
- 将硬件性能 CI 与普通单元测试 CI 分开。

现有测试入口已经按 unit、component、smoke 分层，但导入提交还没有实际 workflow 运行证据。fileciteturn50file0L3-L5 fileciteturn49file0L1-L1

## Phase 1：裁剪产品范围

从主构建中移除：

- Clip；
- ClipQueue；
- VideoEncoder；
- ExportPlan；
- 项目级导出记录；
- 强制代理；
- scope、waveform、vectorscope、histogram；
- 裁剪和截图工作流；
- VMAF 等非实时能力。

保留：

- 播放；
- 暂停；
- 精确 seek；
- 单帧步进；
- 2～3 路同屏；
- 缩放和平移；
- 对齐；
- GPU 差分；
- 设置持久化；
- 诊断日志。

## Phase 2：将 A/B 模型泛化

依次修改：

```text
SourceRole             → SourceId + ComparisonRole
ValidatedSourcePair    → ValidatedComparisonSet
FramePair              → FrameSet
FramePairReady         → FrameSetReady
OpenPairCommand        → OpenComparisonCommand
DirectFrameProvider    → MultiSourceFrameProvider
DualVideoSurface       → ComparisonSurface
ReviewController A/B   → SourceListModel
```

完成后必须先保证两路功能完全回归，再增加第三路。

## Phase 3：实现三路并行解码和显示

- 一个输入一个 `SourceDecodeActor`；
- `FrameSetAssembler` 按 request ID 汇合；
- 三路布局；
- Reference 可选；
- 任意两路可选择为当前差分边；
- 显式 Missing 面板；
- 快速连续逐帧请求合并。

## Phase 4：实现严格和对齐模式

先完成：

1. Strict Index；
2. 手动全局偏移；
3. 自动全局偏移；
4. 掉帧和重复帧检测；
5. 手动锚点；
6. 时间线异常标记。

不要一开始就实现完整无约束 DTW。

## Phase 5：扩展差分和分析布局

- 三组 pairwise diff；
- RGB/Luma/Chroma/Heatmap；
- 阈值 mask；
- ROI 放大；
- 非 pixel-exact 状态；
- 缺失帧不参与差分。

## Phase 6：兼容性和性能强化

- P010/10-bit；
- D3D11VA；
- 旋转和 SAR；
- 可选兼容性代理；
- 三路 1080p60 性能测试；
- 长视频索引缓存；
- 安装包和运行时依赖验证。

---

# 十、建议的最终目录

```text
Toy/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── docs/
│   ├── architecture.md
│   ├── alignment.md
│   └── media-support.md
├── src/
│   ├── domain/
│   │   ├── ComparisonSession
│   │   ├── ComparisonSource
│   │   ├── FrameSet
│   │   ├── AlignmentMap
│   │   └── DifferenceSpec
│   ├── application/
│   │   ├── ComparisonCoordinator
│   │   ├── FrameSetAssembler
│   │   └── CommandsAndEvents
│   ├── media_ffmpeg/
│   │   ├── MediaProbe
│   │   ├── TimelineIndexer
│   │   ├── SourceDecodeActor
│   │   └── FrameCache
│   ├── alignment/
│   │   ├── OffsetEstimator
│   │   ├── SequenceAligner
│   │   └── ManualAnchorMap
│   ├── render_d3d11/
│   │   ├── ComparisonSurface
│   │   └── DifferencePass
│   ├── platform_windows/
│   └── ui_qml/
├── tests/
│   ├── unit/
│   ├── component/
│   ├── integration/
│   ├── ui/
│   └── fixtures/
└── third_party/
    └── notices/
```

在合并过渡期，可以暂时保留：

```text
legacy/
├── DualVideoTool/
└── video-compare/
```

但它们不应进入主 CMake 构建。迁移完成后，只保留必要的第三方声明和合法可复用的测试媒体。

---

# 十一、必须建立的测试矩阵

最终系统至少需要覆盖以下场景：

| 类型 | 测试样例 |
|---|---|
| 标准输入 | 三路相同帧数、相同 CFR |
| 帧数异常 | Prediction 少 1 帧、少 2 帧、多 1 帧 |
| 局部异常 | 中间掉帧、重复帧 |
| 时间偏移 | 全局偏移 -2、-1、+1、+2 |
| 时间线 | 非零起始 PTS、B-frame、VFR、时间戳间隔异常 |
| 空间 | 不同分辨率、不同 SAR、旋转 |
| 编码 | H.264、HEVC、MPEG-4 Part 2、后续 10-bit |
| 错误输入 | 损坏视频、无法 seek、帧索引不完整 |
| UI | A/D、Left/Right、快速连按、焦点切换、时间线拖动 |
| 原子性 | 任何时候都不能出现 Reference 已更新但 Prediction 未更新 |
| 差分 | 三组 pairwise diff、相同帧必须全黑、Missing 不计算 |

验收时最重要的不是“能打开三个视频”，而是以下四个不变量：

1. **同一次显示更新中的所有视频属于同一个标准帧位置。**
2. **逐帧操作永远表示标准帧 ±1，而不是缓存游标移动。**
3. **自动对齐绝不静默隐藏掉帧、重复帧或帧数差异。**
4. **差分图明确区分 pixel-exact 与经过缩放、颜色转换或自动对齐的结果。**

---

# 最终判断

`DualVideoStudio` 已经提供了正确的底层骨架，但它目前仍是一个“工程化的双视频播放器”，而不是“VFI 专用多视频比较器”。最优技术路线不是继续补丁式加入第三路，而是先完成 **A/B 硬编码解除、兼容性检查与对齐分离、`FramePair` 到 `FrameSet` 的核心模型升级**；完成这三件事后，三路同播、三组误差图、帧数差一两帧和手机录屏对齐都会自然落在同一套架构内。
