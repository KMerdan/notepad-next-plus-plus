# Python 第二语言适配

2026-10-11：开发原型使用 Python 3.12 与同一份 JSON-RPC 2.0 / 0.1 profile，复用现有 Qt 管理器、文档、命令、数据与面板服务。统一 metadata 的 `runtime.kind` 接受 `node` 或 `python`，`list` 规则不变。单插件和 Marketplace 都先注册资源到 Uninstalled，安装仍由用户选择，安装后默认禁用。

## 执行与构建

一个 Python 插件使用独立的受控 Node 转发进程和独立 Python 子进程。Qt 通过 Content-Length stdio 发送原始帧，Node 只负责字节流转发、背压与独立父进程监督；Python 自己解析、校验和应答 RPC。Python 主线程或事件循环卡死时，Node worker 仍能响应父应用管道关闭；Qt 重载超时也能终止同组进程。此原型增加了一个解释器进程，其 RSS、线程及多插件成本仍需测量。

构建参数 `NOTEPADNEXT_PLUGIN_PYTHON=/absolute/path/to/python3` 明确选择解释器；CMake 执行隔离版本检查，要求 3.12。`NOTEPADNEXT_TEST_PYTHON` 为同版测试解释器路径，开启第二语言协议、宿主及 GUI 集成测试。没有配置解释器时，资源注册和安装仍可用，激活会显示 RuntimeUnavailable。

Node 和宿主脚本从应用自己的受控目录启动。Python 使用上述绝对路径，不搜索 PATH，不通过 shell 拼接命令，启动参数为 `-I -B -u host.py package-root`。`-I` 排除当前目录、用户 site 与 `PYTHON*` 环境变量，`-B` 避免在只读包内写入 `.pyc`；激活时才加入包根并导入入口模块。适配代码包含在应用资源中；解释器本身尚未随应用分发，不能作为可搬移的正式 Python 发行包。[Python 命令行选项](https://docs.python.org/3.12/using/cmdline.html)

stdout 保留给 RPC，激活前复制私有协议文件描述符并将 fd 1 重定向到 stderr。插件的 `print()` 和直接 `os.write(1, …)` 都进入现有受限诊断通道。隔离选项用于控制导入路径；Python 仍拥有当前用户的文件、网络和进程权限，能力列表只控制宿主 API。

## 作者入口和 API

入口为 `runtime.entry` 指定的 Python 文件，例如 `dist/index.py`。导出 `activate(context)`，可选导出 `deactivate()`；两者可以同步返回或返回 awaitable。命令回调同样支持同步或异步。返回值必须能编码为 JSON；任意 Python 对象、循环引用和非有限浮点值不属于 wire 契约。

```python
async def activate(ctx):
    async def inspect():
        document = await ctx.documents.get_active()
        if document is None:
            return None
        return await ctx.documents.get_snapshot(document["documentId"])
    ctx.subscriptions.append(await ctx.commands.register("author.plugin.inspect", inspect))
```

命令必须在 metadata 声明。Python 包装器用 snake_case，JSON 数据字段仍保持协议中的 camelCase；文本位置使用 UTF-16，Python 字符串索引不能直接充当宿主位置。

| 接口 | 行为 |
| --- | --- |
| `ctx.plugin_id/session_id/generation/capabilities` | 当前插件和会话身份、实际授予的能力 |
| `ctx.commands.register(id, callback)` | 注册已声明的命令，返回异步 `dispose()` 句柄 |
| `ctx.documents.get_active()/get_snapshot(document_id)` | 当前文档信息或快照，包含未保存文本 |
| `ctx.documents.watch(listener)` | 当前文档的有序失效事件；句柄含初始 `document/event_seq` |
| `ctx.documents.apply_edits(document_id, version, edits)` | 复用宿主版本、只读、Unicode 范围与单次 undo 校验 |
| `ctx.webviews.create(view_type, title, position="right")` | 创建受限面板句柄 |
| `panel.reveal()/set_html(fragment, render_context)/post_message(message)/map_resource(resource)` | XHTML 片段、版本化显示、JSON 消息和映射图片；禁用页面自定义脚本 |
| `panel.on_event(listener)` | 面板事件；关闭会回收本地面板句柄及监听器 |
| `ctx.storage.get/update`、`ctx.configuration.get/update` | 本插件和来源隔离的数据；省略 update 的 value 删除 key，`None` 保存 JSON null |
| `ctx.subscriptions` | 作者可添加 disposable；停用时与宿主资源一起逆序释放 |

包装器执行同一份 `wire.schema.json`、`wire-contract.json` 和 `protocol-profile.json`。帧 16 MiB、头 8 KiB、每批 64 项、每方向 64 个未完成请求、JSON 深度 128、原始 ID token 1024 字节；业务预算保持原契约。Python 输出和输入任务另设 128 项／32 MiB 总额，超限停止该连接。RPC ID 保留原始数字／字符串 token，不经浮点数舍入。底层支持双向请求、超时和协作取消；阻塞的插件代码仍由外部进程监督处理。

可运行示例见 [Python Buffer Inspector](../../examples/python-inspector/README.md)。示例发送 XHTML 片段，由宿主添加文档外壳和 CSP。发布工具复用同一 ZIP 与发行校验格式，不执行 Python 导入、pip 或 npm hooks；Python 包不接受 npm runtime dependencies。

## 验证和剩余范围

本机 macOS arm64 / Qt 6.10.0 / Node 24.21.0 / Python 3.12.13 验证了共享协议 fixtures、真实 stdio 反向调用、Unicode 快照、提前到达的文档事件、stdout 隔离、异常诊断和资源释放。真实 Qt 管理器同时运行 JS 和 Python，比较同一份未保存文本，重载主线程卡死的 Python 后确认旧进程退出、JS 会话身份不变，并读取修改后的缓冲区。SIGKILL 父应用测试还确认 Python、同组子进程与 relay 退出，且活进程的包目录先保留、退出后才回收。

详细数字、源摘要和原始日志以 `evidence/PythonAdapterValidation.json` 为准。解释器打包、版本锁与维护、干净系统、Intel／Windows／Linux、原生 Python 扩展依赖、多插件资源成本仍未验收。VS Code 适配是独立的 I3 工作，不能由第二语言测试通过推定兼容。
