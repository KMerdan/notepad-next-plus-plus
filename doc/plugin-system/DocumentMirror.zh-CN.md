# 文档 delta 与同步镜像

这是 I3 VS Code 兼容层的文档基础，当前还没有提供 `require('vscode')`、扩展导入器或完整 VS Code API。原生 SDK 继续使用异步文档服务；内部镜像在初始化后提供同步文本读取，供后续兼容对象使用。

同步需求依据固定的 [VS Code 1.100.0 TextDocument 声明](https://raw.githubusercontent.com/microsoft/vscode/1.100.0/src/vscode-dts/vscode.d.ts)：`getText`、`lineAt`、`offsetAt` 和 `positionAt` 返回本地结果。实现这些方法不代表支持 1.100.0 的全部 API。当前内部 `uri` 仍是字符串，位置和范围仍是普通对象；Uri、Position、Range、事件注册与 VS Code Context 需要后续适配。

## 能力与消息

插件同时声明 `documents.read@1` 和 `documents.delta@1`，才可使用 `context.documents.watchAll(listener)`。旧宿主在包兼容检查或激活时拒绝不支持的必需能力。新增能力与原有 `documents.events@1` 活动文档失效通知独立，握手仍使用 protocol `0.1` / API `0.1.0`。

| 方法 | 行为 |
| --- | --- |
| `nnp.documents.watchAll` | 在 GUI 线程原子建立订阅，返回所有打开缓冲区的 metadata、活动文档 ID、当前序列和固定镜像预算 |
| `nnp.documents.getMirrorSnapshot` | 检查订阅归属于当前会话，返回文本快照及同一 GUI 执行中的序列屏障 |
| `nnp.documents.delta` | 有序通知，带身份、订阅 ID、序列，以及 changed/reset/opened/closed/metadataChanged/activeChanged |

参数与结果使用共享 [wire.schema.json](../../packages/plugin-host/wire.schema.json)。原生服务检查能力、会话状态和订阅归属。订阅计入会话资源账本；显式 dispose、激活失败、停用、重载或退出都会释放。活动文档失效与 delta 订阅合计最多 64 个。

`changed` 的 range、rangeOffset、rangeLength 全部基于修改前文本，单位为 UTF-16 code unit。Bridge 使用 Scintilla BeforeInsert/BeforeDelete 捕获旧坐标，在实际修改后发送新版本。行内位置不包含 CRLF 中间位置；拆开 UTF-8 字符、无法往返转换或超限修改改发 reset，不生成猜测的范围。

宿主 applyEdits 将一次原子批量编辑合并成一个 changed，范围仍全部指向旧文本，按偏移从后向前应用。版本可能跨过多个 Scintilla 修改计数。批量操作的脏状态等元数据在最终文本与版本发布后发送。普通输入、替换、撤销和重做按实际通知分别发送，reason 为 edit/undo/redo；不承诺把整个撤销动作合并成一条 VS Code 事件。

## 初始化与恢复

内部实现位于 [documents.cjs](../../packages/plugin-host/vscode/documents.cjs)。先订阅全部文档，再逐个取得带序列屏障的快照。启动期间缓存事件，每份快照有自己的屏障；它已包含的修改不重复应用，也不影响其他文档的事件。

初始化完成后，文本、行数据与 UTF-16 查询同步返回。行对象是不可变的旧文本视图。读取位置可以 clamp，包括文档末尾和行末；宿主写入仍要求严格可映射的 Unicode 边界。支持 CRLF、单独 CR/LF、末尾空行、emoji 和组合字符。

镜像在修改回调前更新文本。后续兼容命令必须在调用扩展回调前等待 `mirror.flush()`，处理已收到的通知。不能把异步 getSnapshot 直接改名为同步 getText。

版本不匹配、range 与偏移不一致或显式 reset 时，暂停该文档的同步读取并返回 1001；获得快照后明确关闭旧记录、打开新记录并报告 resynchronized。旧记录关闭后保留最后一致文本。序列缺口重新建立订阅和全部文档列表，恢复可能遗漏的打开／关闭事件，并报告 resynchronizedAll。恢复不发布虚构的完整修改事件。

改名或语言变化也关闭／打开镜像记录。关闭缓冲区先移除记录；销毁期间到达的快照由 epoch 检查丢弃，不能复活已关闭镜像。

## 预算与故障

最多镜像 64 个文档、单个快照 2 MiB UTF-8、全部当前文本 4 MiB UTF-8。文档数超限在建立订阅前返回 1007。文本过大返回 1008，不合法 UTF-8 返回 1009；这些文档进入显式 unsupported 列表，不提供替代文本。内容恢复后可重新获取快照。这里限制镜像文本字节，并非整个进程 RSS。

启动缓存和镜像处理队列各最多 128 条／8 MiB。底层 RPC 同时处理最多 64 条消息，输出受运输预算约束。可靠 delta 在过载或处理失败时明确触发连接故障；不会沿用普通失效通知的丢弃策略。镜像自身发生不可恢复故障时暂停读取、释放订阅、调用 fault 回调，flush 返回失败。后续兼容宿主必须把该 fault 接入会话失败处理。这些预算不是 OS 沙箱。

## 验证范围

- [PluginDocumentTests.cpp](../../tests/plugin-documents/PluginDocumentTests.cpp)：真实 Scintilla 删除前 Unicode/多行坐标、原子编辑、undo/redo、无效编码、超限 reset 与恢复、metadata 与有序拓扑。
- [mirror.test.cjs](../../tests/plugin-rpc/mirror.test.cjs)：同步读取、快照屏障、版本/范围恢复、序列缺口、总预算、队列故障和销毁竞态。
- [host.test.cjs](../../tests/plugin-rpc/host.test.cjs)：真实 SDK 的启动事件缓存、带订阅的快照、身份过滤与资源释放。
- [PluginWebviewTests.cpp](../../tests/plugin-webviews/PluginWebviewTests.cpp)：安装后的 Node 插件通过真实 Qt/JSON-RPC/Scintilla 验证两个未保存文档、Unicode 编辑、批量修改、撤销重做、改名、reload/reset、关闭、无效编码恢复和禁用清理；另验证权限与文档数边界。

这些证据完成文档基础增量。I3 仍需要固定版本 API 支持表、导入器、命令别名、同步 Disposable/Webview 代理和获许可的真实扩展样例运行证据。
