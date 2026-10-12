# 应用退出与排队管理操作

最后一个主窗口关闭时，生产应用撤销插件资源、取消未提交的管理操作和安装准备、等待 RPC 及工作线程结束。aboutToQuit 中保留事件投递，5 秒触发强制停止，8 秒结束清理事件循环。会话自身的协议期限和进程树清理可以更早完成。原有期限未改变。

当前六场景证据见 [ApplicationPreparationExitValidation.json](evidence/ApplicationPreparationExitValidation.json)，此前五场景及 Python／插件关闭构建保留在 [ApplicationExitValidation.json](evidence/ApplicationExitValidation.json)。测试将生产 Application、MainWindow、Scintilla 和插件服务编译到独立集成程序；仅测试程序支持场景／临时 profile 选择，发行主程序未新增测试入口。外层测试直接运行该程序，不传可能被 Cocoa 当作文件的 QtTest 位置参数。

| 关闭时的状态 | 验证结果 | 本次退出耗时 |
| --- | --- | --- |
| 插件正常运行、正常 deactivate | 原生命令、文档 watch、WebEngine 面板和所属进程收尾 | 32ms |
| deactivate 主线程死循环 | RPC 期限和进程组清理终止插件及忽略 SIGTERM 的子进程 | 2420ms |
| 重载已排队，旧插件正在停止 | 退出取消后续激活；激活历史仅有原会话 | 2379ms |
| 删除数据的卸载已排队，旧插件正在停止 | 未提交的卸载被取消；安装记录、启用状态和已确认数据保留 | 2378ms |
| 更新候选已登记、激活尚未返回 | 候选面板保持隐藏；退出回收两代进程；后续索引加载恢复原版本和数据 | 2563ms |
| 更新准备尚未完成，实际包存储锁被持有 | 关闭应用取消准备，锁保持到 exec 返回；安装工作结束、原版本与数据保留、没有替换激活 | 2409ms |

六个场景使用实际 Manifest 菜单 QAction 激活插件，真实写入 storage、建立文档订阅和原生 WebEngine 面板。插件及同组子进程设置 SIGTERM 处理器，无响应的主线程无法完成 deactivate 或 activate；清理由宿主承担。每个 PID 均与原生创建时间绑定。exec 返回时 14 个 Node／子进程身份都已退出，会话和资源计数为零；外层测试还要求应用正常终止并再次检查身份，随后加载和校验磁盘中的安装包及数据。

更新准备场景持有实际 `.store.lock`，观察安装请求至少 100ms 保持 busy，然后关闭主窗口。退出取消准备、收到取消结果且 installer 不再 busy 时，锁仍被测试持有，证明收尾无需先释放该锁。此证据覆盖可取消的锁竞争，不代表不可中断的内核文件系统调用。初次测试的等待谓词在成功持锁后再次尝试加锁而误报失败；断言已修正，诊断日志保留，生产代码未改。

激活历史采用追加记录，能够发现同版本重载重复启动，避免覆盖单个 marker 文件而漏掉新会话。更新场景同时检查管理器的 updating 状态和实际 PluginSession 的 activating 状态，不混用两个层次。首次测试误用了状态断言，日志保留在诊断记录，当前完整五场景通过。

当前 Release 原有应用测试及退出目标 **2／2 CTest 通过（18.71 秒）**；原有应用 7 项通过、1 项 Python 跳过，退出目标 8 项通过（含初始化／清理）。此前源码版本的 Python 开发应用 **8 项通过、无跳过（3.72 秒）**，插件关闭应用 **8 项通过（0.41 秒）**；它们保留为对应源码的历史结果，本次仅重新构建并运行两个 Release 目标。源码与测试入口见 [ApplicationExitProbe.cpp](../../tests/plugin-application/ApplicationExitProbe.cpp)、[ApplicationExitTests.cpp](../../tests/plugin-application/ApplicationExitTests.cpp)。

可在启用 Node 和 WebEngine 的测试构建中复现：

```sh
cmake --build build --target PluginApplicationExitTests
NNP_APPLICATION_EXIT_OUTPUT=/absolute/path/application-exit-results.json \
  ctest --test-dir build --output-on-failure -R '^PluginApplicationExitTests$'
```

这些耗时是所记录机器上的退出期限验证，不是隔离性能比较。阻塞文件系统／工作线程、外层 5 秒与 8 秒兜底触发，以及其他平台仍需独立故障证据；当前没有将这些缺口标为完成。该增量只改测试及其 CMake 接线，生产输入与前一轮签名包一致。公证、干净机器、实际最低系统、Intel／Windows／Linux 和完整许可证／对应源码义务仍由原审计追踪。
