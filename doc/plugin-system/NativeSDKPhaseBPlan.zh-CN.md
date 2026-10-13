# SDK Phase B：开放原生编辑器能力

**2026-10-13 的实施前源码盘点与开发计划。** B1–B5 已在开发分支实施，当前能力以 [Phase B 状态](NativeSDKPhaseBStatus.zh-CN.md)与正式 SDK types 为准。本文保留规划基线：应用 `ef1711879946089fb22e5cc430125c65afadd915`、SDK `0.2.1 / 2cc40cf6bbb81b97ec1b203b8c9acaf3b33a02b4`、native API `0.2.0`。下文候选名称与进入条件是原始计划，不替代实施后的具体签名。

Phase B 的目标是让插件复用 Notepad Next ++ 的原生功能，开发文本整理、检查、导航、格式化和编辑辅助工具。继续使用 JSON-RPC；不引入 VS Code API 兼容目标。优先解决真实插件缺少的接口，不逐个搬运 Scintilla 方法。

本文是 Phase B 优先级、依赖、后续项与原始验收范围的维护入口。[当前实施状态](NativeSDKPhaseBStatus.zh-CN.md)记录开发分支能力；[完整架构设计](NativeSDKDesign.zh-CN.md)解释长期约束；[实施前机器盘点](sdk-design/phase-b-api-inventory.json)保存当时源码锚点和摘要。旧 inventory 与候选声明都不能用来判断当前支持情况。

## 1. 已有能力与实际缺口

阶段 A 已经具备：

- 文档列表和丰富元数据、全文快照、版本检查、原子 edits 与一个批次一次 undo。
- 固定命令触发时的 editor/document/window，原子 capture、多选区读取、stream 选区设置与 selection guard。
- 八项共享原生命令：undo、redo、upper、lower、comment toggle、deduplicate、sort、fold all。
- `documents.watch` 与 `watchAll`：已有打开、关闭、激活、metadata、正文失效和 delta/reset 事件。缺少的是选区订阅、丰富元数据事件和明确的保存/重载语义，并非从零建设变更通知。
- 命令/菜单/工具栏 contributions、HTML/fragment/本地服务面板、通用热更新恢复，以及插件自己的配置、数据和日志。

实际类型见 [native.d.ts](../../packages/plugin-sdk/native.d.ts) 与 [index.d.ts](../../packages/plugin-sdk/index.d.ts)。Markdown 和 HTML Preview 可继续使用这些能力；Phase B 不要求它们改 ID、重装或升级协议才能运行。

| 能力域 | 已有原生实现 | 还需要开放的能力 | 优先级 / 批次 |
| --- | --- | --- | --- |
| 事件 | DocumentBridge、Scintilla updateUi、saved/reloaded/lexerChanged | 选区事件；带完整 metadata 的事件；保存与重载原因；订阅释放及合并通知 | P0 / B1，文件事件完成于 B2 |
| 局部读取 | bytePosition、Scintilla 范围读取 | `documents.readRange`，固定 version、返回范围与独立读取预算 | P0 / B1 |
| 定位与激活 | DockedEditor、ScintillaNext::goToRange | `editors.activate/revealRange`；滚动与改变选区分开 | P0 / B1 |
| 文件生命周期 | MainWindow、EditorManager、ScintillaNext | `files.create/open/save/saveAs/close`；明确取消、失败、目标和未保存策略 | P0 / B2 |
| 搜索 | Finder、搜索结果界面 | `search.findAll/count` 首版字面量查询；范围、结果上限、版本和取消 | P1 / B3 |
| 更多编辑命令 | 原生菜单、ScintillaNext、ScintillaSorter | 复制行、移动行、合并/拆分行、反转行、显式注释/取消、缩进/退格缩进；按级折叠 | P1 / B3 |
| 语言与缩进信息 | Lua 语言表、setEditorLanguage、EditorConfig | `languages.list/set/getKeywords`；读取有效 tabWidth/indentWidth/useTabs | P1 / B4 |
| 行尾转换 | MainWindow::convertEOLs | `documents.convertLineEndings`；区分转换正文和新输入的 EOL 模式 | P1 / B4 |
| 用户书签 | BookMarkDecorator | `bookmarks.list/set`，导航复用 reveal；变化通知与明确的用户书签归属 | P1 / B4 |
| 插件高亮 | MarkerAppDecorator、indicator 分配器 | `decorations.create/set/dispose`；插件所有权、配额、释放和热更新清理 | P1 / B5，需要新宿主服务 |
| 重载、另存副本、重命名、回收站 | MainWindow、ScintillaNext | 在文件服务建立后分别定义数据保留、目标冲突和事件契约 | P2 / 后续；重载优先于其余项 |
| 分屏与显示选项 | DockedEditor、ApplicationSettings | 移动已有 dock 至分屏；wrap/zoom/可见字符的显式作用域 | P2 / 后续 |
| HTML/RTF 导出、剪贴板 | Converter、原生菜单、QApplication clipboard | 有界 `exports.render`；剪贴板独立服务与权限 | P2 / 后续 |
| 宏 | MacroManager、Macro、MacroStep | 稳定 ID、可执行步骤分类、版本约束和执行预算后再开放 `macros.list/run` | P2 / 条件项 |
| 文件夹视图 | FolderAsWorkspaceDock | 只读 `folders.getRoot`；不扩展成 IDE workspace | P2 / 有实际需求时 |
| 列编辑、矩形/virtual-space 选区 | ColumnEditorDialog、Scintilla selection | 精确定义视觉列、tab、虚拟空格及事务；当前 setter 仅支持 stream | P2 / 后续 |
| BOM / 通用编码设置 | BOM 检测与保存已有，bomType 无公共 setter | 需要先补原生编码服务；不能称为现成方法的简单桥接 | P2 / 条件项 |
| Base64 / URL 转换、文本格式化 | 原生菜单 + 现有 capture/applyEdits | 先提供 SDK recipe；许多纯文本功能已可由插件实现，无需增加宿主 API | 现有 SDK 即可开展 |
| 补全、保存前格式化、右键扩展 | 原生补全、EditorConfig 保存钩子、菜单已有 | 通用 provider 调度、取消、顺序、贡献条件与资源管理是新宿主工作 | Phase C，按真实需求设计 |

P0 是多个编辑增强插件都需要的基础接口；P1 直接复用原生能力并形成完整工具；P2 的通用收益较低，或需要单独解决新的状态模型。S/M/L 是相对工作量，不是工期承诺。

## 2. 分批开发与依赖

建议实施顺序为 **B1 → B2 → B3 → B4 → B5**。B3/B4 在接口层依赖 B1，并不必须等待 B2；按上述顺序交付便于逐批验收。

| 批次 | 交付结果 | 必要依赖 | 相对工作量 |
| --- | --- | --- | --- |
| B1 | 插件可持续观察选区、读取局部正文、定位结果 | 已有 A 的 IDs/version/selection revision 与订阅基础 | M |
| B2 | 插件可新建结果文档、打开文件、明确保存或关闭 | B1 的事件契约；新的 FileOperations | L |
| B3 | 插件可调用原生搜索和更多行操作 | B1 的范围读取/定位；已有 EditorOperations | M–L |
| B4 | 插件可使用语言、缩进、EOL 和用户书签 | B1 metadata/定位；已有 EditorOperations | M |
| B5 | 插件可为结果建立独立高亮，更新/停用后正确释放 | B1 版本与生命周期；新的装饰资源服务；TODO 示例另依赖 B3 | L |

### B1：事件、局部读取与导航

候选入口：`documents.readRange`、新的 native 文档事件订阅、`editors.watchSelections`、`editors.activate/revealRange`。

1. 保留旧 `watch/watchAll` 的数据形状；增加协商后的 native 事件入口。新入口复用宿主 ID/version/sequence，实现订阅时快照与事件交接、丰富 metadata、选区 revision、通知合并、溢出 reset 和重新读取。保存/重载事件由 B2 的真实文件结果补齐，不提前模拟成功。
2. `readRange` 接受 documentId、expectedVersion、UTF-16 range 和协商预算，返回实际 range/text/version。整段读取超限应拒绝；长范围另用有版本约束的分页约定。不能内部调用全文 snapshot 再 slice。当前位置转换已通过 `bytePosition` 使用原生行和 code-unit 查询；可复用它，但超长单行的扫描成本也必须受限。
3. `revealRange` 只展开必要折叠并滚动；选区变化继续通过 `setSelections`。现有 `goToRange` 会调用 `setSelection`，不能直接当成纯滚动实现。激活/聚焦由明确参数控制，后台更新不抢走输入焦点。
4. 新订阅跟随插件会话 generation。停用、崩溃、卸载和热更新时释放；恢复已有面板时重新订阅，拒绝旧会话的迟到消息。

验收：Unicode/CRLF 与半 surrogate 边界；切换 tab 后仍指向原目标；事件订阅交接无漏失；重复通知可合并且最终状态一致；dispose 后无通知；超过 2 MiB 文档的小范围读取不生成全文副本，超长行/超预算明确拒绝；纯 reveal 不改选区，不抢焦点。超过 2 MiB 的**写入**仍受现有 applyEdits 全文预检限制，B1 不改变这条边界。

交付插件示例：Text Inspector 第一版，展示当前选区/局部文本信息，点击结果定位。SDK 提供通用模板和测试；完整插件在 Marketplace 独立验证。

### B2：文件生命周期

候选入口：`files.create/open/save/saveAs/close`。先做单文档操作；重载作为文件服务的后续扩展，不一次开放批量保存、强制丢弃和磁盘删除。

- 提取 FileOperations，原生 UI 与 RPC 共用操作实现。目标始终是固定 documentId，而非弹窗返回后的 `currentEditor()`。文件路径/URI 与打开文档 ID 保持区别；已打开同一文件时的复用、激活和新建文档规则明确记录。
- 成功结果包含 documentId 和最新 metadata；用户取消返回明确 `cancelled`，未执行可返回 `skipped`，IO/权限/冲突以结构化错误表达。清洁文档保存应是明确 no-op。当前 bool 返回值把 Save As 取消与写入失败混在一起，需要重构。
- `save/saveAs` 明确版本及弹窗后的再验证；EditorConfig 的原生 `aboutToSave` 处理可能修改正文，返回真实保存后的 version。SDK 暂不提供跨进程阻塞式 `willSave` 插件回调。
- `close` 首版允许 `prompt` / `ifClean`。弹窗期间文档被修改、关闭或会话更新时重新验证；已取消的用户输入不能转成继续关闭。结果表明关闭、取消或跳过。
- 插件关闭最后一个文档时保留主窗口，补空白页或返回明确的保留结果。原生菜单可继续遵守用户的退出偏好。当前 `MainWindow::closeFile` 会按 `exitOnLastTabClosed` 关闭整个窗口，不能原样透传。

验收：未命名文档 Save As 成功/取消、不可写目标、干净文档 no-op、后台目标与换 tab、对话框期间正文变化、关闭 dirty 文档取消、关闭最后标签、更新期间迟到写请求；所有结果与实际磁盘/文档状态一致。写请求失联或超时继续遵守 unknown outcome，不自动重试。

交付示例：文本整理插件把转换结果放进新文档并由用户保存。主仓只用通用文件契约测试插件，不依赖 Markdown/HTML 的业务实现。

### B3：搜索与共享编辑命令

候选入口：`search.findAll/count` 和 `core.execute` 的新增稳定命令。

- SearchService 使用独立参数：documentId/version、字面量文本、大小写/whole-word、range、结果上限、取消。结果返回 version、UTF-16 ranges 与是否完整/续读信息。count 同样有预算，不能伪装成免费的全文件扫描。
- 首版按现有文档预算实现可取消的短任务；大文件搜索是独立扩展。Scintilla 访问保持 GUI 线程，小批处理后才让出事件循环。Finder 会改 search flags/target，服务需隔离或恢复这些状态，不影响用户的查找对话框。
- 替换先返回计划 edits，再通过已有 guarded applyEdits 一次提交。发生 version/selection 冲突时整个批次不写入。多文档批量工具返回逐文档结果，不宣称全局事务。
- 从原生菜单提取复制行、上/下移、合并/拆分、反转、显式注释/取消、缩进/退格缩进和按级折叠。每项定义 selection semantics、文档/视图效果、readonly、undo 和所需 grants。菜单与 RPC 调同一实现。
- 空行清理、尾部空白和保存钩子目前涉及正则。先审查有界替代算法或预算，再进入命令目录。任意插件 regex 搜索不能直接包装同步 `QRegularExpression::match`；Promise/AbortSignal 本身不能中断该调用，需独立隔离/可中断设计后开放。

验收：跨 Unicode/CRLF 的范围、重叠规则、结果截断、取消/文档关闭/版本变化；用户查找状态不变；每个新增编辑命令与原生菜单结果、选区、undo 一致；readonly 不写入。对恶意正则的处理留在条件扩展的验收范围。

### B4：语言、EOL、有效编辑参数与书签

候选入口：`languages.list/set/getKeywords`、`editors.getOptions`、`documents.convertLineEndings`、`bookmarks.list/set`。

- 语言列表来自编辑器语言表；公开稳定 ID 和显示名的映射。实施盘点确认已有 metadata 使用小写语言 ID，例如 `text/html`，`Normal Text` 映射为 `plaintext`；本轮沿用该映射，并单独提供原生显示名称。set 只接受已登记语言，不公开任意 Lua 执行。
- 语言切换会重建 lexer，并可能应用缩进/tab 默认值；事件和结果必须覆盖这些影响，保留 EditorConfig 已设置的优先级。先开放有效缩进参数读取，通用全局设置写入不在此批。
- EOL 转换使用固定目标、version/readonly 检查和一个 undo。正文转换与后续输入的 EOL mode 是两个概念，分别定义；metadata 更新不依赖偶然的文本通知。
- 书签首版提供读取和显式增/删指定行，范围按 version 验证，导航复用 B1。书签是用户资源，插件停用不清除它；插件临时标记使用 B5 装饰。剪切/删除书签行可用已有 edits 组合，剪贴板另有作用域。

验收：未知语言拒绝、EditorConfig 与语言默认值交互、lexer/metadata 事件、混合 EOL 转换与 undo、书签随正文编辑的原生行为、两个插件读取同一用户书签且停用不清除；UI 与 RPC 结果一致。

### B5：插件专属装饰与生命周期

候选入口：`decorations.create/set/dispose`；首版只提供受限的文本范围高亮和下划线，不引入任意 editor widget、gutter 按钮或复杂 diagnostics provider。

- 建立 DecorationsService，资源归属于 plugin/session generation/document。set 接受 expectedVersion 与有界 ranges，旧正文的结果不能画到新版本。
- 扩展 indicator 分配器的释放和耗尽处理；已有 RangeAllocator 只有 request、没有 release，耗尽会 assert/返回 -1。定义每插件/每文档的 style/range 配额，避免占用内置标记、IME 和历史 indicator。
- dispose、文档关闭、停用、崩溃、卸载和热更新都释放高亮与槽位。热更新恢复面板后由新会话重建装饰；不跨 generation 复用失效句柄，不动用户书签。

验收：两个插件不同资源互不清除；配额超限不崩溃；旧版本和旧 generation 拒绝；连续 100 次热更新/释放后资源占用回到基线；通用面板仍恢复位置和状态。

交付示例：TODO Navigator，使用 B3 搜索、B5 高亮与 B1 点击定位。它是 Marketplace 的业务插件，主仓只验证通用搜索/装饰契约。

## 3. 后续项的进入条件

| 后续项 | 首个可交付接口 | 进入条件 |
| --- | --- | --- |
| reload/saveCopy/rename/trash | 优先 `files.reload`，提示/ifClean 与 reset 明确 | B2 稳定；对磁盘失败、取消、别名路径、正文保留各自定义语义 |
| 分屏 | `editors.moveToSplit` | 原生实现是移动已有 dock；不承诺 clone/shared-buffer 或多宿主窗口 |
| 显示选项 | 有作用域的 options 查询，再决定允许哪些设置 | 当前 zoom 同时作用多个 editor，wrap/可见字符连到全局设置；先决定 view 与 global 的归属 |
| 导出/剪贴板 | 有范围/version/输出预算的 `exports.render(html|rtf)` | 先返回内容；系统剪贴板与写磁盘独立授权和结果，不能藏进 document read |
| 宏 | 稳定 ID 的 `macros.list`，再做受限 run | MacroStep 存 raw Scintilla 消息；需可执行步骤分类、取消/次数预算与固定目标。不能直接开放 replayTillEndOfFile |
| BOM/其他编码 | 先定义原生编码与转换服务 | 目前有检测/保存与 bom getter，缺少公共设置服务；单独验证不可表示字符、保存失败与 dirty 状态 |
| 列编辑/virtual space | 受限 selection setter 或专用列编辑操作 | 视觉列与 UTF-16 不同；定义 tab、宽字符、虚拟空格和矩形行为，保留一次 undo |
| 大文件写入 | 独立的有界验证/事务设计 | readRange 不会解除现有 snapshot/edit preflight 限制；先证明编码、delta、undo 与成本边界 |
| 文件夹/高级 providers | 只读 root；之后按插件实例需求设计 provider | 不引入完整 IDE 项目模型；补全、format-on-save、右键贡献分别建设调度与取消机制 |

Python 的新 native profile 绑定可在契约稳定后增加，使用同一 JSON-RPC 和 fixtures；不与 B1 基础 API 同时扩大语言绑定范围。

## 4. 契约、分仓与 CI

计划先在 SDK 仓库评审候选 TS types、请求/结果/错误/事件 schemas 和跨语言 fixtures，再在主仓实现。建议新能力协商为 native API `0.3`，继续保留 `0.2/0.1`；这是版本提案，SDK 包版本独立决定。JSON-RPC 2.0、metadata 格式和 wire profile 不因多几个方法自动升级。新增 capabilities、limits 和 feature detection 必须可协商，不要求旧插件迁移。

所有批次遵循：稳定 ID 与 UTF-16 范围；目标固定且不回退到当前 tab；写入有 version/selection guard；作用域明确；短 GUI 操作；取消和 unknown outcome 明确；插件资源随 generation 释放。原生 UI 和 RPC 复用服务，但交互政策可以由调用者明确选择，例如最后标签关闭。

| 仓库 | 本阶段应验证什么 | 不作为该仓的业务门槛 |
| --- | --- | --- |
| 主仓 | 原生服务、RPC schema/grants、ID/版本/取消/undo、真实 Qt/Scintilla 行为、会话清理；支持的 CI 平台与 core-only/插件关闭配置 | Markdown/HTML/TODO 的解析、渲染与业务测试 |
| SDK | types/客户端一致性、grants/limits、事件 dispose、错误表达、模板构建/调试/打包、共享 fixtures | 用 test host 替代真实编辑器语义验收 |
| Marketplace | 示例业务正确性、SDK 固定依赖、repo 注册/安装/更新/删除、面板恢复及生成的 Release 包 | 决定主仓原生 API 是否按契约工作 |

单批实施步骤：候选契约和 fixtures → 原生共享服务/RPC → SDK 客户端、模板与文档 → 真实宿主契约验收 → Marketplace 示例验收 → 更新 SDK pin 与当前状态。跨仓使用可追溯的固定提交/包，不先发布空有类型的新 API。

每批的通用门槛：旧 0.1 Markdown/HTML 及 0.2 text-tools 仍可使用；未授予的调用拒绝；热更新前的迟到请求不产生新写入/资源；文档/API 由真实宿主测试佐证。新增原生能力的测试使用通用 fixtures，不把某个 Marketplace 插件加入主仓必需 CI。

## 5. 下一步最小交付

**先实施 B1。** 第一份实现变更包含 native 选区/metadata 事件契约、真正有界的 readRange、纯 reveal 与激活；同时补 SDK fixture 和 Text Inspector 模板。它直接改善检查、导航和局部预览类插件，又为文件、搜索和装饰建立共同基础。

原始盘点阶段只完成计划，随后用户授权完整实施 B1–B5。机器盘点保存规划基线的 source hash，当前实现需以状态、正式 types 和宿主验收核对，不能把历史快照当作当前支持列表。
