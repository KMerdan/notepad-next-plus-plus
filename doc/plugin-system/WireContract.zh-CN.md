# 业务协议契约 0.1

消息外壳遵守 [JSON-RPC 2.0](https://www.jsonrpc.org/specification)：请求以 id 关联响应，通知没有响应；业务方法使用命名对象参数。本项目的生命周期、能力、会话身份和预算是该协议之上的应用契约。

规范文件放在受控宿主中：

- [wire.schema.json](../../packages/plugin-host/wire.schema.json)：33 个方法的 params/result 定义及 DocumentInfo、TextSnapshot、位置、编辑、delta、消息提示、兼容 HTML/资源、图片和错误模型，采用 [JSON Schema 2020-12](https://json-schema.org/draft/2020-12/json-schema-validation)。
- [wire-contract.json](../../packages/plugin-host/wire-contract.json)：每个方法的方向、请求／通知、能力、允许状态、效果、错误、取消语义和预算。
- [protocol-profile.json](../../packages/plugin-host/protocol-profile.json)：握手确认的运输和业务预算。

Qt `WireContract` 从编译进程序的资源读取相同文件。Node `wire.cjs` 和 Python `nnp_wire.py` 从随应用复制的宿主资源读取。三个求值器只实现这个闭合、受控 Schema 使用的词汇：局部 `$ref`、type、const、enum、oneOf、对象 properties/required/additionalProperties、数组 items/uniqueItems/minItems/maxItems、字符串长度/pattern、数值 minimum/maximum。它们没有加载外部 Schema 或执行任意 Schema 的接口。

`x-nnp-maxUtf8Bytes` 是本项目追加的运行时预算；标准 JSON Schema 求值器可以忽略此注解。Qt、Node 和 Python 另外执行它，超限返回 `1008`。其他不符合参数 Schema 的请求返回 `-32602`；不符合结果 Schema 的成功响应转为失败，避免 SDK 把无效值交给插件。角色、文档版本、路径 containment、声明命令、资源归属、能力和持久化状态仍由相应服务检查；仅通过 Schema 不会授予权限。

初始化协商 protocolVersion=`0.1`、apiVersion=`0.1.0`，并确认完整 limits 对象。版本列表可同时提供其他版本和 `0.1`，宿主只选 `0.1`；仅提供未知版本、不同 API、修改／缺少／新增预算字段都会在插件模块 import 前失败。能力返回值必须与授予集合相同。尚未承诺其他版本兼容；增加一个 major profile 时必须提供独立 Schema 和跨版本 fixtures，不能改名后静默接受旧语义。

除了 `nnp.documents.event`、`nnp.documents.delta`、`nnp.webviews.event`、`nnp.window.messageEvent` 和 `nnp.request.cancel`，业务方法都必须使用请求并接收确认。Qt 和 Node 拒绝以通知执行资源登记、生命周期或业务修改，不发送错误响应给通知。未知业务方法仍由 JSON-RPC 端点返回 `-32601`。取消让调用者停止等待，不能撤销已经提交的文档编辑或数据写入；可取消的只读工作可丢弃迟到结果。生命周期超时则终止所属进程并撤销会话资源。

`activePublished` 表示已经完成 activate 并通过更新确认／资源发布屏障。文档编辑、配置与数据写入、命令执行要求此状态。激活中允许只读获取文档／数据和暂存命令、订阅、面板；激活失败会全部撤销。

共享 [wire-fixtures.json](../../tests/plugin-rpc/wire-fixtures.json) 覆盖每个方法的合法参数、额外字段、缺少身份／必填项、结果，以及未来 API、预算变化、能力重复、不安全整数和 nullable context。Qt、Node 与 Python 都读取这一文件。独立 `python-jsonschema` 4.26.0 的 Draft202012Validator 检查了 Schema 和全部 180 个 fixtures，当前结果记录在 `evidence/VSCodePersistenceSchemaValidation.json`；自定义字节预算另有跨语言测试。真实 Node 宿主另验证“不兼容初始化不 import 模块”，SDK 另验证 undefined 删除 key 的 wire 编码。

这份契约描述当前受限宿主能力。页面脚本需要明确的兼容 Webview 配置与授权；workspaceState、完整 VS Code API、跨插件命令和 OS 沙箱没有因此自动获得支持。

新增 documents.delta@1 使用全部文档快照屏障与旧文本 UTF-16 delta；可靠通知过载或处理失败会明确触发连接故障。版本／序列异常恢复、同步镜像与预算详见 [文档镜像契约](DocumentMirror.zh-CN.md)。

messages@1 使用立即确认的创建请求和可靠选择通知；消息属于激活会话，停用清理结束 SDK 等待。命令执行计时暂停规则、预算与 VS Code 字符串动作 overload 见 [兼容配置](VSCodeCompatibility.zh-CN.md)。

webviews.compat@1 新增 `createCompatible/setDocument/mapAsset/setTitle`，同时依赖 webviews@1。`nnp.webviews.event` 的 viewState 为可靠所属面板事件；Node/Python 求值器同样禁止过载时静默丢弃。更换 HTML、新旧 revision、加载期间消息排队与正常关闭竞态见 [Webview 兼容语义](VSCodeCompatibility.zh-CN.md#webview-发布资源与消息)。
