# 1.1.0 发布执行状态（2026-07-30）

本节是当前发布工作的权威状态；后文保留产品审查、历史判断和待办依据。若后文与本节冲突，以本节为准。

| 发布项 | 当前状态 | 已有证据 / 下一步 |
| --- | --- | --- |
| 1.1.0 版本、RC、ZIP、MSI 一致性 | 已完成并本地验证 | EXE、ZIP、MSI 均为 1.1.0 |
| 两路 120 FPS backend 数量判断 | 已完成并有回归覆盖 | 报告包含 `expected_source_count=2/3` |
| Source A（SourceId 0）错误归属 | 已完成并有专门回归测试 | `AttributesInvalidSourceASequenceOffsetToSourceZero` |
| Debug / Release | 本地通过，等待 PR 证据 | 两个预设各 371 项通过 |
| format-check / lint | 本地通过，等待 PR 证据 | clang-format、clang-tidy、qmllint 通过 |
| 真实 2/3 路视频 GUI smoke | 已通过 | 使用 NoRefEval validation 中的 120 FPS H.264 素材 |
| D3D11VA / zero-copy | 本地通过 | 2/2 通过 |
| shutdown soak | 本地通过 | 20 轮通过 |
| ZIP / MSI 生成与版本属性 | 本地通过 | `verify-release-version.ps1 -Tag v1.1.0` 通过 |
| self-hosted runner | 已在线 | `Sonwe-RTX4090`，交互式 Session 1 |
| 1080p120 runner 素材 | 已补齐 | 2/3 路均使用 75 秒、1920×1080、120 FPS H.264 |
| GitHub PR 与 CI | 待执行 | 建立 PR 后运行 Build/Test 与 Quality |
| 1080p120 硬件门禁 | 待执行 | 由 Hardware and Performance workflow 在指定 SHA 上运行 |
| 1.0.0 → 1.1.0 MSI 升级 | 待执行 | 需要提升权限的 runner packaged-smoke |
| Authenticode 签名 | 阻塞 | 仓库尚无签名 secrets，本机证书库也无可用代码签名私钥 |
| GitHub Release v1.1.0 | 待执行 | CI、硬件、升级和签名全部通过后创建并发布 |

发布原则保持 fail-closed：不得用 WARP、普通单元测试或虚拟显示上的推测结果替代真实硬件门禁；不得把未签名产物描述为已签名发布。

---

# 核心结论

**目前已经达到我们最初设定的核心使用预期，可以进入真实日常试用；但还没有达到“无需额外验证即可合并并正式发布”的标准。**

更准确地说：

| 判断维度                    | 结论             |
| ----------------------- | -------------- |
| 短视频逐帧审查体验               | 达到预期           |
| 拖入、Wipe、分割线和 Loading 优化 | 达到预期           |
| 高级功能可理解性                | 基本达到预期         |
| 60/120 FPS 技术适配         | 代码达到预期，硬件结果待验证 |
| MSI 新装与旧版升级设计           | 基本达到预期         |
| 正式发布流水线设计               | 达到预期           |
| GitHub CI 与真实发布证据       | 尚未完成           |
| 当前是否直接发布                | 不建议            |

综合评分可以理解为：

```text
产品功能完成度：约 93%
日常使用准备度：约 90%
工程实现完成度：约 88%
正式发布准备度：约 80%
```

---

# 一、上一轮发现的阻断项已经修复

这次最新修改已经处理了此前最关键的四个问题。

## 1. 版本升级链路已经修正

工程版本已经从 `1.0.0` 提升为 `1.1.0`，避免与已经发布的 DualVideoStudio 1.0.0 发生同版本 MSI 升级冲突。

Windows 资源版本也不再手工硬编码，而是通过 `VCStation.rc.in` 从 CMake 的 `PROJECT_VERSION` 自动生成：

```text
CMake 1.1.0
→ EXE FileVersion 1.1.0
→ ProductVersion 1.1.0
→ MSI 1.1.0
```

这一项已经达到预期。

## 2. 两路 1080p120 门禁已经修正

性能程序现在根据实际输入数量计算预期 decoder 数量：

```cpp
expectedSourceCount = third.has_value() ? 3 : 2;
```

并要求实际 backend 数量与其一致。

因此两路 1080p120 不会再因为写死“三个 backend”而必然失败。

## 3. Source A 错误归属已经修正

此前 `SourceId=0` 被错误地当作“没有 source”。现在 `serviceError()` 明确接收：

```cpp
std::optional<domain::SourceId>
```

所以：

```text
nullopt = 没有具体 source
0       = Source A
1       = Source B
2       = Source C
```

逻辑已经正确。正式合并前仍建议增加一个专门的 Source A 回归测试，防止以后再次把 0 当作空值。

## 4. Release workflow 已经形成完整门禁

新的发布流程不再只是“构建后上传文件”，现在包含：

* tag 与 CMake 版本验证；
* Debug 构建与测试；
* Release 构建与测试；
* format-check；
* clang-tidy/QML 质量检查；
* 同一 commit 的硬件与性能门禁；
* 下载 DualVideoStudio 1.0.0 MSI；
* 真实升级到 VCStation 1.1.0；
* EXE 与 MSI Authenticode 签名；
* packaged smoke；
* shutdown soak；
* ZIP/MSI SHA-256；
* Draft GitHub Release。

硬件 workflow 也支持 `workflow_call`，会精确 checkout 指定 SHA，并验证实际测试 commit 与预期一致。

这套设计已经达到正式工程发布流程应有的结构。

---

# 二、七项核心用户需求已经基本达到预期

## 1. 拖入视频：达到主要预期

已经支持：

* 拖入两个视频；
* 拖入三个视频；
* 拖入一个 `.dvsproj`；
* Unicode 路径；
* 重复文件检测；
* 缺失文件检测；
* 项目和视频混合拖入拒绝；
* 打开前确认 A/B/C 顺序和 Reference。

界面也有完整的拖入覆盖层。

这一项已经可以满足主要工作流程：

```text
拖入 GT + Prediction
→ 确认顺序
→ 立即开始比较
```

剩余的小缺口是：

* 单独拖入一个视频时只会设置 A；
* 不能直接拖到 B/C 卡片替换指定 source；
* 已有 A 后再拖一个文件，不会自动填入 B。

这些属于便利性增强，不影响当前日常使用。

---

## 2. 开始菜单和文件关联：达到预期

WiX 已经创建：

```text
开始菜单
└── VCStation
    └── VCStation
```

同时注册：

```text
.dvsproj → VCStation.exe "%1"
```

MSI 脚本已经验证：

* 旧 DualVideoStudio 1.0.0 安装；
* 升级到 VCStation；
* 旧 ARP 条目被删除；
* 旧 EXE 被删除；
* 新快捷方式存在；
* 新文件关联存在；
* CLI probe 成功；
* 卸载后清理。

CTest 也已经有独立的旧版升级测试入口。

代码层面已经达到预期。

---

## 3. Compare 分割线和 Wipe：达到预期

Side-by-side 和 Three-up 都加入了明确的分割线。

Wipe 模式已经深入到 D3D11 renderer，不是单纯在界面上覆盖两个控件。它支持：

* A/B；
* A/C；
* B/C；
* 可拖动分割位置；
* 同步缩放；
* 同步平移；
* ROI；
* 保持相同 canonical frame。

WARP 测试还验证了 A/C 和 25% 分割位置。

这一项完成质量较高，符合肉眼观察插帧边缘、纹理和重影的需求。

---

## 4. 不明按钮和控制区：基本达到预期

原来挤在一行中的：

```text
Offset
Auto Align
Find Drops
Anchors
Apply
Reset
```

已经移动到默认折叠的高级对齐 Inspector。

按钮名称也变得更明确：

* `Estimate global frame offset`
* `Analyze missing / duplicate frames`
* `Edit manual anchors`
* `Apply frame offsets`
* `Return to Strict Index`

每个操作还有用途说明和示例。

文件、比较和分析功能也已经放入标准菜单。

### 仍未完全达到的细节

当前界面存在英文和中文混用：

```text
Open videos…
Save review project
另存评测项目…
Export Bad Case…
```

此外，顶部工具栏采用横向 Flickable 解决 960 像素窗口下的溢出，但没有明显滚动条或“右侧还有内容”的提示。

因此这一项属于：

> 功能理解问题已经解决，但视觉和本地化仍需要最后一轮产品打磨。

---

## 5. ffprobe：达到预期

最终用户包已经不再部署外部：

```text
ffmpeg.exe
ffprobe.exe
```

应用和 CLI 都直接使用 FFmpeg 动态库。

打包阶段还会主动检查这两个 EXE 不得出现在最终包中。

这一项已经完整达到预期。

---

## 6. 高频逐帧不再反复全屏 Loading：达到预期

现在已经把状态拆成：

```text
busy         = 打开、保存、状态变更
framePending = 当前只是在请求另一帧
```

界面仅在没有任何已显示帧时使用全屏 Loading；已有旧帧时保持旧画面。

如果新帧超过约 140 ms 仍未准备好，只在右上角显示轻量的：

```text
Fetching latest frame…
```

导航请求在上一请求未完成时仍然可以提交，旧请求被 supersede，最终呈现最新请求。

测试也覆盖了“只有最新 navigation context 完成后才清除 framePending”。

这正是我们希望获得的体验。

---

## 7. 针对 60/120 FPS 短视频：基本达到预期

预取窗口会根据 canonical FPS 自动调整：

```text
普通帧率：3 前 / 1 后
约 60 FPS：6 前 / 2 后
约 120 FPS：12 前 / 3 后
```

同时新增：

* 两路 1080p120、60 秒；
* 三路 1080p120、60 秒；
* 三路 1080p60、5 分钟；
* 三路 4K30 Main10、5 分钟。

对于绝大多数 1 分钟以内、少数 2 分钟的素材，这一技术路线是合理的。

当前 Signature cache 默认允许 50,000 个 signature，能够覆盖大约：

```text
3 路 × 120 FPS × 2 分钟 = 43,200
```

因此与真实工作负载比较匹配。

剩余优化是让预取同时根据分辨率和实际内存预算缩小窗口，但这不是当前短视频工作流的阻断项。

---

# 三、现在还不能直接宣称“完全达到正式发布预期”

## 1. 缺少该分支的 GitHub PR 验证证据

目前没有发现 `feature/vcstation-userplan` 对应的 PR。

Build/Test 只会在：

* Pull Request；
* push 到 `main`；

时自动执行。

因此，虽然代码和测试定义已经完善，但现在还不能确认最新分支已经实际通过：

* Debug；
* Release；
* component/e2e；
* format；
* lint；
* QML；
* clang-tidy。

这是当前最主要的“证据缺口”，而不是功能缺口。

## 2. 120 FPS 和升级门禁存在，但尚需真实运行结果

现在的测试逻辑已经正确，但测试定义存在并不等于硬件门禁已经通过。

正式结论需要以下四个结果：

```text
1080p120-2source  PASS
1080p120-3source  PASS
DualVideoStudio 1.0.0 → VCStation 1.1.0 PASS
签名 MSI packaged-smoke PASS
```

尤其是 120 FPS 的真实结果必须来自登录交互桌面的 D3D11VA runner，不能由 WARP 或普通单元测试替代。

## 3. USERPLAN 已与代码事实不一致

当前 `USERPLAN.md` 仍然写着：

* 版本还是 1.0.0；
* 两路 120 FPS 门禁必然失败；
* Source A attribution 未修；
* Release workflow 缺少版本和硬件门禁。

但这些问题在代码中已经修复。

文档还保留旧的 P0 清单和“不可合并”结论。

这会误导后续开发者，也可能让自动编码员工继续重复修复已经完成的内容。合并前应将 USERPLAN 改成：

```text
Completed
Validated locally
Pending CI
Pending hardware
Deferred P1/P2
```

---

# 四、仍可继续改善，但不阻挡内部使用

## 1. 单文件拖放体验

建议让单文件自动填入下一个空 source，并支持拖到指定 A/B/C 卡片替换。

## 2. 统一界面语言

建议源字符串统一英文，通过 Qt Linguist 提供 `zh_CN`；或者短期全部改中文。当前混合语言会削弱专业感。

## 3. Bad Case 导出

当前导出：

```text
comparison.bmp
evidence.json
```

建议改为 PNG，并记录：

* view mode；
* difference edge；
* metric/gain；
* Wipe position；
* ROI；
* threshold。

## 4. 旧用户设置迁移

应用数据目录已从：

```text
%LocalAppData%\DualVideoStudio
```

切换到：

```text
%LocalAppData%\VCStation
```

MSI 能升级程序，但用户偏好不会自动迁移。建议首次启动时检测旧设置并一次性导入，或者明确说明设置会重置。

## 5. Provider 仍部分阻塞

虽然 SourceDecodeActor 已改成 callback completion，但 Provider 仍在自己的 worker 内等待一个请求的全部 source completion。

对于当前 2～3 路短视频没有明显问题，可以留到后续架构优化。

---

# 五、最终验收判断

## 已达到的预期

* 拖入视频即可比较；
* 开始菜单入口；
* `.dvsproj` 文件关联；
* Side-by-side 分割线；
* Wipe Compare；
* 高级对齐折叠与解释；
* Save As 含义明确；
* 外部 ffprobe 移除；
* 高频逐帧不再全屏闪烁；
* 60/120 FPS 动态预取；
* 120 FPS 性能门禁定义；
* MSI 旧版升级测试；
* 签名和完整 Release workflow。

## 尚未达到的最后条件

* 最新代码尚无正式 PR；
* 最新 Debug/Release/Quality checks 尚无 GitHub 结果；
* 最新两路/三路 1080p120 尚无真实硬件结果；
* 最新旧版 MSI 升级门禁尚无实际运行记录；
* USERPLAN 仍是过期结论。

---

# 最终决策建议

**当前版本已经达到我们期望的产品形态，可以交给真实使用者进行内部试用。**

但发布决策应设为：

```text
内部试用：通过
进入 PR：通过
合并 main：等待 CI 全绿
发布 VCStation 1.1.0：等待硬件、升级和签名包门禁通过
```

现在不需要继续增加大功能。下一步应集中完成：

> **更新 USERPLAN → 建立 PR → 跑通 Debug/Release/Quality → 跑通 2/3 路 1080p120 → 验证 1.0.0 MSI 升级 → 发布 1.1.0。**
