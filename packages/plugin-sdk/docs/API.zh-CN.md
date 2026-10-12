# API 与 metadata

完整 API 以 [index.d.ts](../index.d.ts) 为准。SDK 0.1.0 对应宿主 API 0.1.x；开发工具版本与 metadata 的宿主 API 范围分别管理。宿主通过 `activate(context)` 注入 API，插件不需要自行处理 JSON-RPC。

## metadata

模板提供 `notepadnext-plugin.json`，以 `list: false` 标识单插件。Marketplace 使用同一格式的 list 数组。资源注册只读取 metadata，安装阶段再校验构建入口、能力、版本、依赖和文件内容。

| 字段 | 含义 |
| --- | --- |
| manifestVersion / id / name / version | 格式版本、唯一身份、展示名称、插件 SemVer |
| engines.notepadnext / engines.api | 编辑器与宿主 API 兼容范围 |
| runtime.kind / runtime.entry | Node 或 Python；包内构建入口 |
| capabilities.required / optional | 必需与可选的宿主 API 能力 |
| activationEvents | onCommand 等延迟激活触发条件 |
| contributes.commands | 命令 ID 与展示标题，注册回调时必须一致 |
| contributes.menus / toolbar | 原生 Plugins 菜单与工具栏入口 |

[metadata Schema](../schemas/manifest.schema.json) 已配置到模板的 VS Code JSON 校验。Schema 提供结构提示，实际安装器还执行语义、版本和文件校验。name 修改不影响命令身份；id 修改必须同步命令、activationEvents、菜单、工具栏和源码中的注册 ID。创建工具已处理这些绑定。

## 常用 API

| API | 能力 | 用法 |
| --- | --- | --- |
| commands.register(id, callback) | commands@1 | 注册 metadata 声明的命令，返回 Disposable |
| window.showMessage(severity, text, items?) | messages@1 | 激活后的命令中显示消息，等待选择或关闭 |
| documents.getActive / getSnapshot | documents.read@1 | 当前文档信息、包括未保存内容的快照 |
| documents.watch | documents.events@1 | 活动文档失效通知；接到事件后重新读快照 |
| documents.watchAll | documents.delta@1 + documents.read@1 | 多文档事件与版本化镜像 |
| documents.applyEdits | documents.write@1 | 按 UTF-16 位置与 expectedVersion 应用编辑 |
| webviews.create | webviews@1 | 原生右侧或底部面板 |
| panel.setHtml(html, context) | webviews@1 | 允许的 XHTML 片段；context 携带文档 ID/版本 |
| panel.mapResource | webviews@1 | 包内资源或当前文档相对图片映射 |
| webviews.createCompatible | webviews@1 + webviews.compat@1 | Qt 6.6+ 支持的完整 HTML 与消息桥 |
| storage / configuration | storage@1 / configuration@1 | 插件自己的 JSON 数据和配置 |

检查 context.capabilities 使用可选能力。required 能力不满足时包不能在该宿主激活。storage/configuration 的 update(key, undefined) 删除 key，compareExchange 可用于并发冲突检测；这些写操作要在成功激活后执行。插件数据有宿主限额。

普通 Webview 的 XHTML 片段由宿主添加页面外壳、CSP、主题及字体；原生接口不接受任意脚本和插件样式。模板 Markdown-it 禁用 raw HTML，以 XHTML 输出。完整 HTML 需要声明独立 compatible 能力；SDK 模拟器不实现该分支。

## 清理

activate 中可把命令句柄放入 context.subscriptions。自己的订阅、面板、定时器和 AbortController 在 deactivate 中释放；面板关闭事件也应停止对应 watch，重开再创建。宿主对其追踪的资源有最终清理。

宿主能力约束 API 访问。插件进程运行于用户账户权限，当前进程分离不等同于 OS 文件系统沙箱。安装与注册不执行插件；启用并触发激活后才运行代码。

## Python

宿主支持 Python 的同一协议适配。当前开发包的 create/debug 模板面向 Node/TS/JS，Python 作者使用已有宿主 context 与显式配置的 Python 3.12。Python 打包复用 nnp-package/nnp-release，模块应放在 dist，运行时不自动 pip install。主仓库的 PythonAdapter.zh-CN.md 和 python-inspector 示例记录详细契约。
