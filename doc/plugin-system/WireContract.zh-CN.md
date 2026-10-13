**原生 API 0.2 增补（2026-10-12）：** 文档 list/getInfo、editor capture/selection、core 与原生 HTML 入口见 [Native API](../../packages/plugin-sdk/docs/NativeAPI.zh-CN.md)。新命令使用独立 invokeNative；旧 callback args 保留。Python prototype 仍协商 0.1。以下历史 fixture 数量和独立 jsonschema 记录是当时的验证，不代表新增全部方法的当前数量。

# 业务协议契约 0.1

消息外壳遵守 [JSON-RPC 2.0](https://www.jsonrpc.org/specification)：请求以 id 关联响应，通知没有响应；业务方法使用命名对象参数。本项目的生命周期、能力、会话身份和预算是该协议之上的应用契约。

规范文件在 SDK 仓库 `schemas` 中维护；主应用通过固定 submodule 与同步脚本生成受控宿主快照：

- [wire.schema.json](../../packages/plugin-host/wire.schema.json)：45 个方法的 params/result 定义及 DocumentInfo、TextSnapshot、位置、编辑、delta、消息提示、兼容 HTML/资源、图片和错误模型，采用 [JSON Schema 2020-12](https://json-schema.org/draft/2020-12/json-schema-validation)。
- [wire-contract.json](../../packages/plugin-host/wire-contract.json)：每个方法的方向、请求／通知、能力、允许状态、效果、错误、取消语义和预算。
- [protocol-profile.json](../../packages/plugin-host/protocol-profile.json)：握手确认的运输和业务预算。

Qt `WireContract` 从编译进程序的资源读取相同文件。Node `wire.cjs` 和 Python `nnp_wire.py` 从随应用复制的宿主资源读取。三个求值器只实现这个闭合、受控 Schema 使用的词汇：局部 `$ref`、type、const、enum、oneOf、对象 properties/required/additionalProperties、数组 items/uniqueItems/minItems/maxItems、字符串长度/pattern、数值 minimum/maximum。它们没有加载外部 Schema 或执行任意 Schema 的接口。

`x-nnp-maxUtf8Bytes` 是本项目追加的运行时预算；标准 JSON Schema 求值器可以忽略此注解。Qt、Node 和 Python 另外执行它，超限返回 `1008`。其他不符合参数 Schema 的请求返回 `-32602`；不符合结果 Schema 的成功响应转为失败，避免 SDK 把无效值交给插件。角色、文档版本、路径 containment、声明命令、资源归属、能力和持久化状态仍由相应服务检查；仅通过 Schema 不会授予权限。

初始化协商 wire protocolVersion=`0.1`，API 按 manifest range 选择 `0.1.0` 或 `0.2.0`；仍包含 0.1 的宽泛旧 range 优先保留 0.1，排除 0.1 的明确 native range 才进入 0.2，并确认完整 limits。Node 支持两个 API；Python prototype 只支持 0.1。未知 profile 或修改／缺少／新增预算字段在 import 前失败；grant 返回值必须一致。0.2 使用独立 invokeNative 对象回调，0.1 保持 positional args。新增 native 方法及 selectionGuard 在 0.1 会话中拒绝；类型和 profile 不能混用。

除了 `nnp.documents.event`、`nnp.documents.delta`、`nnp.webviews.event`、`nnp.window.messageEvent` 和 `nnp.request.cancel`，业务方法都必须使用请求并接收确认。Qt 和 Node 拒绝以通知执行资源登记、生命周期或业务修改，不发送错误响应给通知。未知业务方法仍由 JSON-RPC 端点返回 `-32601`。取消让调用者停止等待，不能撤销已经提交的文档编辑或数据写入；可取消的只读工作可丢弃迟到结果。生命周期超时则终止所属进程并撤销会话资源。

`activePublished` 表示已经完成 activate 并通过更新确认／资源发布屏障。文档编辑、配置与数据写入、命令执行要求此状态。激活中允许只读获取文档／数据和暂存命令、订阅、面板；激活失败会全部撤销。

共享 [wire-fixtures.json](../../tests/plugin-rpc/wire-fixtures.json) 覆盖每个方法的合法参数、额外字段、缺少身份／必填项、结果，以及未来 API、预算变化、能力重复、不安全整数和 nullable context。Qt、Node 与 Python 都读取这一文件。独立 `python-jsonschema` 4.26.0 的 Draft202012Validator 检查了 Schema 和全部 180 个 fixtures，当前结果记录在 `evidence/VSCodePersistenceSchemaValidation.json`；自定义字节预算另有跨语言测试。真实 Node 宿主另验证“不兼容初始化不 import 模块”，SDK 另验证 undefined 删除 key 的 wire 编码。

这份契约描述当前受限宿主能力。页面脚本需要明确的兼容 Webview 配置与授权；workspaceState、完整 VS Code API、跨插件命令和 OS 沙箱没有因此自动获得支持。

新增 documents.delta@1 使用全部文档快照屏障与旧文本 UTF-16 delta；可靠通知过载或处理失败会明确触发连接故障。版本／序列异常恢复、同步镜像与预算详见 [文档镜像契约](DocumentMirror.zh-CN.md)。

messages@1 使用立即确认的创建请求和可靠选择通知；消息属于激活会话，停用清理结束 SDK 等待。命令执行计时暂停规则、预算与 VS Code 字符串动作 overload 见 [兼容配置](VSCodeCompatibility.zh-CN.md)。

webviews.compat@1 新增 `createCompatible/setDocument/mapAsset/setTitle`，同时依赖 webviews@1。`nnp.webviews.event` 的 viewState 为可靠所属面板事件；Node/Python 求值器同样禁止过载时静默丢弃。更换 HTML、新旧 revision、加载期间消息排队与正常关闭竞态见 [Webview 兼容语义](VSCodeCompatibility.zh-CN.md)。

webviews.restore@1 增加宿主调用插件的 `nnp.webviews.restore`，描述待恢复面板并要求专用回调重新创建。webviews.service@1 增加 `nnp.webviews.createService`，仅接受带会话所有权证明的 IPv4 loopback HTTP 服务；依赖 webviews@1、Qt WebEngine 6.6+。这是 API 0.1.0 的能力协商扩展，未声明的插件不会收到恢复调用。
