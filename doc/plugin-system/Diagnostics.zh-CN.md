# 插件诊断与故障分类

原生运行时保存结构化诊断，管理器 Installed 的详情显示当前故障类别和原因，Logs 窗口显示会话记录并随新日志更新。重启软件后可以查看上一进程的记录和最近故障摘要；历史故障不会自动阻止重新启用插件。

## 记录范围

每条记录包含 UTC 时间、插件 ID、安装来源仓库、会话 ID、事件类型、故障类别和受限消息。原生命令调用另记录方法与耗时，进程退出记录退出码。宿主不记录 RPC 参数、结果或文档正文；插件 stderr 保留作者主动输出的内容及命令异常堆栈。对用户显示的运行／命令错误最多 2048 个 UTF-16 单元，截断不拆开 surrogate pair；通用 JSON-RPC 错误对象仍按既有传输 profile 接收。实例间的日志读取按插件 ID 与来源仓库共同筛选，同 ID 的其他仓库不会显示旧来源日志。

类别由失败发生的位置确定，不从英文报错文本猜测：包完整性无效、能力缺失、运行时缺失、初始化失败、激活失败、协议故障、进程崩溃、健康超时、命令失败、更新失败和进程所有权失败。RPC worker 在关闭挂起请求之前投递协议／进程分类；激活已失败后，排队命令的连接关闭回调不覆盖原始故障。正常禁用、重载和退出不产生新的插件故障。

日志不是插件 API，也不增加业务 wire 方法。stdout 仍专用于 JSON-RPC；既有 stderr 的 16 KiB/s 投递预算继续生效。

## 内存与磁盘预算

`PluginDiagnostics::Service` 只在 GUI 线程进行受限入队。工作线程合并记录、读取、轮转和原子保存；正常合并期限 250 ms，退出时立即开始冲刷。队列最多 128 条，每轮最多处理 64 条，单条序列化上限 24 KiB、消息上限 8 KiB，UTF-8 截断保留字符边界并附上标记。队列过载优先保留故障摘要；下一批说明跳过的记录数量。存储失败最多重试两次，随后报告不可用并允许退出；后续成功写入会恢复可用状态。

磁盘位置是 `QStandardPaths::AppDataLocation/plugins/diagnostics/`。只有 `current.json` 和 `previous.json` 两份快照，每份最多 512 KiB，正常持久文件合计最多 1 MiB。当前快照保留最多 256 条事件和 64 个来源命名空间的最近故障摘要；摘要消息最多 1 KiB。管理器每插件展示最近 32 KiB 文本，并可附上一进程的故障摘要。插件卸载不删除应用诊断，它们按全局预算自然轮转；插件数据／配置删除仍由独立 data 服务执行。

快照保存使用禁用 direct-write fallback 的 QSaveFile，文件权限限制为当前用户读写。先保存上一份有效快照，再原子替换当前快照；读改写由进程间 QLockFile 排他保护。一次原子保存可能额外占用至多一个 512 KiB 临时文件。

进程被强制结束可能留下 QSaveFile 的具名临时文件。下次写入先取得排他锁，在最多 512 个目录条目的检查预算内，删除匹配 `current.json.<6字符>` / `previous.json.<6字符>` 的安全、受限普通文件，再开始新写入。未知名字、链接、目录和超预算文件保留；无法安全判断时停止写入。这一命名规则依据当前固定 Qt 实现，并有真实强制结束 writer 的本机验证；其他 Qt／平台仍须执行矩阵测试。[Qt 6.10 QSaveFile 实现](https://github.com/qt/qtbase/blob/v6.10.0/src/corelib/io/qsavefile.cpp)、[Qt 6.10 临时文件实现](https://github.com/qt/qtbase/blob/v6.10.0/src/corelib/io/qtemporaryfile.cpp)

损坏、未知格式或超限快照不被新日志覆盖。管理器 Logs 显示已保存诊断不可用的具体原因，当前内存日志继续工作。恢复时先保留异常文件副本，再移开异常快照并重开应用；不要将丢失诊断记录误认为插件执行成功。

## 验证范围

`PluginDiagnosticTests` 覆盖跨实例持久化、来源隔离、最后故障替换、全局轮转／文件预算、损坏记录保留、锁冲突、路径链接、Unicode 截断、封闭记录结构、队列过载故障优先、冲刷／存储恢复和重复强制结束原子 writer。

`PluginWebviewTests::persistentFailureDiagnostics` 运行真实 Node 宿主，分别制造激活异常、损坏 stdout 协议和包完整性失败，验证 Installed 详情／Logs 的类别、磁盘摘要和新 engine 的历史恢复。健康超时、崩溃恢复和更新回退继续使用原有真实会话测试。当前证明限于本机 macOS arm64 / Qt 6.10 / Node 24；跨平台、阻塞文件系统、同时写入的广泛故障及完整性能预算仍在审计中。

`largeCommandFailureIsBounded` 通过真实反向 RPC 返回约 4 MiB 的含 emoji 错误，验证显示上限、字符边界、命令失败分类和会话继续运行。`retryWhileFailedProcessDrains` 在旧进程尚未退出时立即重试失败的激活，验证等待旧资源清理后创建单个新会话。Node 宿主另验证命令异常堆栈只出现在 stderr。

加入上述边界用例后完整 14/14 CTest 通过（134.76 秒）；诊断目标 10 个、原生 WebEngine 目标 17 个 QtTest 用例全部通过，100 次生命周期循环 76.45 秒。原始日志与该次源文件摘要为 `evidence/diagnostics-conformance.log` 和 `evidence/DiagnosticsSourceSha256.json`。该记录保留了真实执行的边界，不能用于替代其他平台／发行验收。
