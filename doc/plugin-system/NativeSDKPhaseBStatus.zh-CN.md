# Native SDK Phase B implementation

2026-10-13：B1–B5 已实现，当前只交付 native API **0.3.0**。SDK 0.3.0 已发布；所有 Marketplace 插件统一使用当前 profile。旧 0.1/0.2 negotiation、位置参数命令回调、VS Code 适配器和 Python 原型已移除。JSON-RPC 2.0、wire profile 0.1、manifestVersion 1 仍是当前传输/metadata 格式，和 API 版本不是同一件事。

- B1：有界 UTF-16 readRange、文档/选区订阅、定位和编辑选项。
- B2：create/open/save/saveAs/close，固定 ID/version、取消/跳过及未知 IO outcome。
- B3：可取消字面量搜索/计数、分页及 18 项原生编辑命令。
- B4：语言、缩进、EOL 转换和用户书签。
- B5：session/document 所有的高亮与下划线，配额、资源释放、更新后验证再发布。

API 和限制由独立 SDK 的 [Phase B 指南](../../packages/plugin-sdk/docs/PhaseB.zh-CN.md)及 [native.d.ts](../../packages/plugin-sdk/native.d.ts)维护。下载与安装见 [Downloads](../Downloads.md)。主仓测试只用通用宿主 fixtures；Marketplace 验证四个产品插件。

原开发分支的本地验收输入见 [验证记录](evidence/NativeSDKPhaseBValidation.json)；该记录包括旧 profile 检查，不代表精简版本的测试数量。当前 CI 以分支 GitHub checks 为准。100-cycle fixture 继续验证 owned Node identities、session/panel/资源与焦点连接；Qt 全进程 pipe/thread/RSS 作为诊断，不冒充每插件资源所有权。编码转换、宏、分屏、providers 仍是后续范围。
