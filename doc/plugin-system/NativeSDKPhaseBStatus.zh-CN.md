# Native SDK Phase B implementation

2026-10-14：B1–B5 已实现，当前交付 native API **0.3.0** 和 SDK **0.3.2**。Marketplace 产品和示例统一消费正式 SDK Release。旧 0.1/0.2 negotiation、位置参数命令回调、VS Code 适配器和 Python 原型已移除。JSON-RPC 2.0、wire profile 0.1、manifestVersion 1 是当前传输与 metadata 格式，与 API 版本分别维护。

- B1：有界 UTF-16 readRange、文档/选区订阅、定位和编辑选项。
- B2：create/open/save/saveAs/close，固定 ID/version、取消/跳过及未知 IO outcome。
- B3：可取消字面量搜索/计数、分页及 18 项原生编辑命令。
- B4：语言、缩进、EOL 转换和用户书签。
- B5：session/document 所有的高亮与下划线，配额、资源释放、更新后验证再发布。

SDK 0.3.2 还定义了 `command`、`editor-tool`、`editor-preview`、`window-tool`、`background-service` 五类插件。预览由用户手动打开，每插件/窗口一个共享 dock，跟随匹配的活动文档并保留各文档状态。关闭 dock 或最后一个匹配文档时回收预览进程；安装与启用状态保留。更新只恢复存活的组件，不重放 Open。API 包含 `nnp.previews.getSource` 与 `nnp.webviews.reloadSource`，其 source/version 检查拒绝过时渲染。所有权与状态限额见 [SDK 生命周期契约](../../packages/plugin-sdk/docs/PluginLifecycle.zh-CN.md)。

API 与限制由独立 SDK 的 [Phase B 指南](../../packages/plugin-sdk/docs/PhaseB.zh-CN.md)及 [native.d.ts](../../packages/plugin-sdk/native.d.ts)维护。下载与安装见 [Downloads](../Downloads.md)。主仓 CI 使用通用宿主 fixtures；Marketplace 独立验证 Markdown/HTML 两个产品和 Text Inspector/TODO 两个 SDK 示例。后两者不在默认产品目录。

应用 [Encoding 菜单](../Encoding.md)已实现文件解码、重新加载和保存编码转换。SDK 文档编码字段可表示 Unicode 与旧式编码；编码变更方法、宏、分屏和 providers 仍是后续 API 范围。

原开发分支的本地验收输入见 [验证记录](evidence/NativeSDKPhaseBValidation.json)；该记录包括旧 profile 检查，不代表当前测试数量。当前 CI 以对应提交的 GitHub checks 为准。100-cycle fixture 验证 owned Node identities、session/panel/资源与焦点连接；Qt 全进程 pipe/thread/RSS 作为诊断，不当作每插件资源所有权。

当前类别、预览所有权、正式发布包与 Marketplace URL 验收见 [发布验收记录](evidence/PluginCategoryLifecycleReleaseAcceptance.json)。
