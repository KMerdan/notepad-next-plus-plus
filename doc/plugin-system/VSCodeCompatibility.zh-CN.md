# VS Code 兼容配置 0.1.0

固定 API 声明基线为 VS Code **1.100.0**，运行端仍是 NotepadNext 原生协议/API **0.1 / 0.1.0**。完整类型声明只用于编译选定样例，运行时支持下表中的子集。此版本实现命令、文档、Qt 消息、有界的完整 HTML Webview、Memento 状态和插件 Global 配置。工作区、配置、迁移等限制见 [状态与配置适配](VSCodePersistence.zh-CN.md)；原方案剩余要求继续保留，不能据此宣称完整 VS Code 兼容。

| API | 当前行为与限制 |
| --- | --- |
| `require('vscode')` | 已导入插件包内 CommonJS 模块获得会话所属适配器；停用恢复加载器。原生 SDK 不变。暂不支持 ESM 的 `import 'vscode'`。 |
| `Disposable`、`Disposable.from` | 同步、幂等 `dispose()`；聚合清理继续处理其他资源，再报告异常。远端清理由会话追踪。 |
| `EventEmitter` | 同步有序通知，支持 `thisArg`、订阅 Disposable 和 subscriptions 数组。监听异常进入诊断，不阻止其他监听。 |
| `Position`、`Range` | 不可变位置/范围、比较、平移、替换、包含、交集与并集；位置限非负安全整数。文档接口使用 UTF-16。 |
| `Uri` | `file/parse/from/joinPath/with/fsPath/toString/toJSON`；本机路径、`untitled:` 和受支持的 scheme。`file()` 只接受绝对路径；凭据、端口和平台 UNC 兼容未提供。 |
| `ExtensionContext` | `subscriptions/extensionUri/extensionPath/extensionMode/asAbsolutePath/globalState/workspaceState`。路径接口只返回包内路径；Memento 按下表的来源与单编辑会话语义保存。存储目录 URI、Settings Sync 及其他未列出的 Context 字段明确拒绝；原生异步存储 API 有独立契约。 |
| `commands.registerCommand` | 同步返回 Disposable；只注册静态 Manifest 别名。后台完成 Qt 登记；激活应答等待全部登记确认。先 dispose 再确认、注销后同名注册串行处理。 |
| `commands.executeCommand` | 仅本插件已注册命令，保留参数和 `thisArg`；跨插件命令和 VS Code 内置命令明确拒绝。 |
| `window.showInformationMessage/showWarningMessage/showErrorMessage` | 非模态 Qt 纯文本提示；支持字符串动作。Promise 在用户选择/关闭时返回字符串/undefined，停用清理并结束等待。对象动作、modal 和 MessageOptions overload 暂不支持。 |
| `window.activeTextEditor` | 当前镜像文档的编辑器代理，无文档时 undefined。`viewColumn` 为 One；Qt 多 dock 与 VS Code 编辑列映射尚未实现。 |
| `workspace.textDocuments`、文档打开/变更/关闭与当前编辑器事件 | 同步镜像。事件回调与命令读取前应用可靠 delta；断序/版本冲突显式重建，旧记录关闭。预算与边界见 [文档镜像](DocumentMirror.zh-CN.md)。 |
| `TextDocument` | `uri/fileName/languageId/version/isDirty/isClosed/isUntitled/lineCount` 与 `getText/lineAt/offsetAt/positionAt/validatePosition/validateRange`。范围返回本配置的 Position/Range。保存、编码、词法范围及完整 EOL API 未实现。 |
| `TextEditor.edit` | 同步 builder 收集 insert/replace/delete，按旧版本一次提交，成功后更新镜像；事务一键 undo/redo。要求 `documents.write@1`；只接受默认 undoStop 语义、同步 builder、最多 256 edits。冲突/只读/已关闭返回 false。 |
| `globalState/workspaceState` | 同步 get/keys、确认落盘的 update，独立保存全局与单个持久编辑会话状态；需要 storage@1。激活写入、命名 workspace 和同步暂不支持。 |
| `workspace.getConfiguration/onDidChangeConfiguration` | 插件声明默认值及 Global 覆盖，get/has/inspect、点号读取与确认后事件；需要 configuration@1。范围、条件写入、迁移和预算见 [状态与配置适配](VSCodePersistence.zh-CN.md)。 |
| `window.createWebviewPanel` | 同步返回所属代理，远端创建、reveal、title/html 属性写入与 URI 映射串行确认。创建确认前 dispose 会跳过后续发布并回收晚到句柄。需要 Qt WebEngine 6.6+ 与 `webviews@1`、`webviews.compat@1` 两个能力。 |
| `WebviewPanel` | `viewType/webview/title/viewColumn/visible/active/reveal/dispose/onDidDispose/onDidChangeViewState`。真实 Qt 可见性/焦点通知；目前只映射逻辑 One/Active，Beside/Two/Three 明确拒绝。隐藏时保留 DOM，显式要求丢弃隐藏上下文的选项不支持。 |
| `Webview` | `html/options/cspSource/asWebviewUri/postMessage/onDidReceiveMessage`。完整 HTML 文档；本地 JS/CSS/图片在声明的资源目录内映射。options 在创建时拍快照，运行期修改明确拒绝。 |
| 浏览器 `acquireVsCodeApi()` | 每页只能获取一次；支持 `postMessage/getState/setState`。状态是有界 JSON，保存最近提交到宿主的值，在同一面板换页时恢复。同一面板换页及热更新/Reload/回滚时可恢复；window.registerWebviewPanelSerializer 映射到 webviews.restore@1。正常关闭和应用退出后不保留。 |
| 未实现 API | `UnsupportedApi`（业务错误 1005）含 API 名称与配置版本并写诊断，不以空实现宣称兼容。类型类未列出的方法没有实现。`then/__esModule` 是模块互操作字段。 |

消息最多同时 8 个，文字最多 8 KiB UTF-8，动作最多 8 个、每个最多 1 KiB。创建先返回所属 messageId，选择用可靠 `nnp.window.messageEvent` 通知，早于创建应答的选择被缓存。只在成功激活且资源已发布时展示。停用、卸载和失败清除所有所属窗口，不保留旧会话通知。

`lineAt(Position)` 使用其行号并拒绝越界；`validatePosition` 才负责位置夹取。该区分按 [1.100.0 文档实现](https://raw.githubusercontent.com/microsoft/vscode/1.100.0/src/vs/workbench/api/common/extHostDocumentData.ts) 验证，不能把二者都夹到末行。

通常命令执行预算为 5 秒。当前会话有提示框打开时，执行计时暂停；最后一个关闭后恢复剩余预算。这是会话所有权规则，不能判断插件是否实际 await 了特定提示。每会话仍只接受一个并行命令；健康检测、退出计时和取消不暂停。禁止插件在 activate 中等待用户提示，以免破坏初始化/更新发布屏障。

## Webview 发布、资源与消息

兼容完整 HTML 使用独立能力和 `createCompatible/setDocument/mapAsset/setTitle` 契约；原生 Markdown 的 XHTML fragment API 保持独立。激活或更新验证阶段只暂存页面，成功发布后才开始运行其脚本。每页使用新 revision，桥消息与 state 必须匹配所属会话、面板和当前 revision。用户关闭窗口可先于已经发出的 HTML/title 更新，远端 `1004` 只结束该面板；权限、Schema 和其他错误仍使发布失败并报告诊断。

宿主通过响应头强制 CSP：外部网络、任意 file URL、frame/object/connect 和弹窗均不开放。JS 受 `enableScripts` 控制；内联 JS/CSS 还需页面中的有效 nonce。页面自己的 meta CSP 可进一步收紧，不能放宽响应头。来自另一面板的资源会被拦截；同一面板可懒加载声明目录内的相对 JS/CSS/图片。

每会话最多 8 个面板，每面板最多 16 个目录、64 个资源。JS/CSS 须为有效 UTF-8，单文件最多 2 MiB；图片沿用 PNG/JPEG/WebP、8 MiB 和 4096 像素限制。资源合计 32 MiB；HTML 最多 6 MiB。资源必须是包内目录下的正规文件，规范化路径与 symlink 目标均接受包含性校验。目录列表为空表示禁止所有本地资源。URI 同步产生，映射应答必须吻合，属性写入随后才发布 HTML。

代理最多排队 128 次操作、8 MiB HTML；消息与 state 单值最多 64 KiB。`postMessage` 返回 true 表示宿主接纳，不是浏览器已处理的确认。正在加载时消息有界排队；更换 HTML 清除旧页的待发消息。若需要先恢复状态再马上换页，插件必须等消息桥已建立且值提交；本配置不保证尚未交给宿主的 state 能跨导航保存。

## 导入已编译扩展

`nnp-import-vscode <built-extension> <new-output> [--webviews] [--storage] [--configuration] [--resource=media]` 只读取静态文件，生成统一 `list:false` metadata、静态命令别名、原生菜单和导入来源哈希。输出目录需在源目录外且尚不存在；源代码与 hooks 不执行。默认只复制 main 所在构建目录及许可证；资源目录必须显式声明，最多 16 个，拒绝重复、重叠、保留路径及 symlink。`--webviews` 显式加入两个 Webview 能力，不根据代码猜测授权。包按现有安装生命周期使用，安装不会自动启用。

当前导入器要求 publisher/name、SemVer、支持基线的 `engines.vscode`、已编译 CommonJS main、1–32 个静态命令及许可证。engine 范围只接受完整 `x.y.z`、`^x.y.z` 和 `~x.y.z`；其他表达式拒绝，不猜测兼容。允许隐式命令激活或对应 onCommand 事件。运行时 npm 依赖、原生 addon、symlink、除有界 configuration 外，其他 contribution/activation 均需后续支持配置，暂时拒绝。资源限制为 2048 路径、单文件 16 MiB、总文件 64 MiB；安装器另做原生 Schema、包完整性和包含性校验。

## 当前验收样例

[HelloWorld 源码](../../examples/vscode-hello-world/src/extension.ts) 与 package.json 来自 Microsoft MIT 样例，提交固定为 `65c1c44800eeac6fb065a54823ee5ae62c79540a`，源码没有改动；[来源哈希与许可证](../../examples/vscode-hello-world/provenance.json) 随库保留。TypeScript 5.9.3 使用固定 API 声明编译；构建前校验上游哈希，不执行上游 npm scripts。

Node 契约测试、Qt 安装/菜单/重载/停用测试与生产 Application 测试验证该命令路径。另有真实 Scintilla Unicode 编辑、单次 undo/redo、等待超过 5 秒的消息选择，以及等待期间停用清理。核心验证记录见 [核心证据](evidence/VSCodeCoreValidation.json)。

[Webview 样例](../../examples/vscode-webview/README.md)来自同一固定 MIT 提交的 Cat Coding，保留精确上游文件和 SHA-256。浏览器 JS/CSS 未修改；扩展源码保留上游 serializer 注册以恢复热更新，远程 GIF 换成声明目录内的自有 PNG fixture。它验证完整 HTML、CSS/JS/图片、计数器、双向消息，以及安装后的关闭重开、重载、停用和卸载。该样例是明确记录改动的移植，不冒充原样运行；HelloWorld 仍保留未修改的命令样例证据。Webview 新增 Schema 的 33 方法/174 fixtures 已独立验证，见 [Schema 证据](evidence/VSCodeWebviewSchemaValidation.json)。有界持久状态与插件全局配置已另行接入，见 [状态与配置适配](VSCodePersistence.zh-CN.md)。I3 状态继续为 partial。
