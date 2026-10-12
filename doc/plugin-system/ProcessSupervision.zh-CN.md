# 父应用退出与插件进程所有权

正常退出由 `PluginSession` 请求 deactivate / shutdown，随后 `RpcProcess` 回收插件进程组。父应用被强制结束时不会执行这些析构和事件处理，需要另一个退出通道。

## Unix 父生命周期管道

`RpcProcess` 启动插件前创建专用管道。应用只保留写端，插件继承固定 fd 3 的读端；其他进程不能通过正常 exec 继承应用写端。Qt 的子进程 modifier 建立独立进程组，将读端映射为 fd 3，并清除它的 close-on-exec 标志。Qt 6.6 及以上另用 `CloseFileDescriptors` 关闭 fd 4 及以上的继承描述符；Qt 6.5 保留逐端关闭和 close-on-exec 规则。

受控 Node 宿主在 initialize、加载插件之前启动独立 worker，并等待监听就绪。worker 用 libuv 的异步管道读取；写端因应用退出而关闭时，即使插件主线程正执行无限循环，worker 也能向 Qt 创建的进程组发送 SIGKILL。监听自身故障也会结束进程组。stdout 仍只传 JSON-RPC；监听 worker 不向该通道转发输出。

管道 EOF 不依赖父 PID、轮询间隔或插件主事件循环。不能在 worker 中用 `process.exit()` 代替进程信号，因为它只退出该 worker；也不能用无限阻塞的 `fs.readSync()`，它会阻塞 Node 正常退出时的 worker 终止。[Node 24 worker threads](https://nodejs.org/download/release/v24.21.0/docs/api/worker_threads.html)、[Qt 子进程 modifier](https://doc.qt.io/qt-6/qprocess.html#setChildProcessModifier)

Windows 继续使用配置 `KILL_ON_JOB_CLOSE` 的 Job Object，不创建 Unix 管道。Windows 的真实异常退出执行证明仍列在完整验收中，不能由 macOS 测试推定。插件自行脱离进程组或使用同用户 OS 权限干预监督不在可信插件模型的保证范围内。

## 包目录与实际运行进程

`owner.lock` 的所有者是应用进程；应用退出后，不能仅依据这个锁删除插件还在读取的目录。`PackageLease` 在插件 initialize 之前，原子保存实际插件进程的 PID、平台和启动时间：

```json
{
  "schemaVersion": 1,
  "digest": "<package-content-sha256>",
  "process": {"pid": 12345, "platform": "macos", "start": "<seconds>:<microseconds>"}
}
```

`ProcessIdentity` 将 PID 与启动时间一起比较，避免 PID 被另一个进程复用后误判插件仍存活。macOS 使用内核 `p_starttime`；Linux 使用 boot ID 与 `/proc/<pid>/stat` 的 start ticks；Windows 使用 `GetProcessTimes` 的创建时间。僵尸／已结束进程允许回收；检查权限或平台不确定时保留。Linux/Windows 分支目前只有实现，没有该平台执行证据。[Apple XNU 进程结构](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/proc.h)、[Linux proc 文档](https://docs.kernel.org/filesystems/proc.html)、[Windows GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)

回收线程先检查 lease，再读取原子安装索引。应用所有者已退出而实际插件仍存活或状态未知时，继续保留摘要。实际进程退出后，下一次回收移除失效 lease 和未引用包。失效锁下的损坏记录也保留并输出诊断，避免把无法验证的记录当成可安全删除的文件。

## 故障验证

`PluginParentCrashTests` 使用真实 `RpcProcess` / Node 宿主：插件先启动同组子进程，再令主线程无限循环。测试暂停整个插件进程，强制结束父应用，确认独立回收器仍保留代码目录；恢复插件后，监听 worker 结束主进程和同组子进程，最后确认失效使用记录与未引用包可回收。

Python 原型增加同一测试的第二个数据行：Python 主线程无限循环，Node relay 的监听 worker 位于独立进程中。暂停 relay 后强制结束父应用，仍存活的 relay 使用记录继续保留代码目录；恢复 relay 后确认 relay、Python 和 Python 启动的同组子进程全部退出，再回收失效记录与目录。管理器另验证卡死 Python 的 Reload，保持同时运行的 JS 会话身份不变。结果与原始日志见 `evidence/PythonAdapterValidation.json`，解释器部署与其他平台仍待验收。

`PluginPackageTests` 另验证启动时间不匹配、实际进程存活、异平台记录、损坏运行记录及退出后回收。Node 宿主测试覆盖正常 shutdown、主线程阻塞时的 EOF 和非法 fd 在导入插件前被拒绝。这些证明针对本机 macOS arm64 / Qt 6.10 / Node 24.21；完整平台、句柄／RSS 和新增 worker 成本仍须独立测量。
