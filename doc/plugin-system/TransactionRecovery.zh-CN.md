# 安装与更新的进程中断恢复

安装包发布和 Installed 索引提交是两个步骤。完整包先按内容摘要发布，由 prepared lease 保留；索引原子登记后才成为安装记录。单纯出现一个包目录不代表已经安装。未确认的更新记录保存候选、原版本与更早的回滚历史，重启恢复必须在命令发布和激活之前完成。

当前实现与规则分别位于 [PackageStore](../../src/plugins/packages/PackageStore.cpp)、[InstalledPluginRegistry](../../src/plugins/packages/InstalledPluginRegistry.cpp) 和 [包回收契约](PackageCollection.zh-CN.md)。以下为本机 macOS 27.0.1 arm64、Qt 6.10.0 Release 的实际进程证据，完整原始结果见 [TransactionRecoveryValidation.json](evidence/TransactionRecoveryValidation.json)。

| 中断点 | 进程终止后的预期结果 | 已验证的状态 |
| --- | --- | --- |
| 复制进行中 | 未登记安装；未完成的 stage 可以回收 | 在生产复制回调中暂停，文件大于 0 且小于 4MiB；存活锁保护 stage，终止后删除一个临时目录 |
| 包已发布，尚未登记 | 未登记安装；完整但未引用的包可以回收 | 存活 prepared lease 保留包；终止后删除孤立包和失效 lease |
| 安装索引已提交 | 保留已安装版本，不执行插件代码 | 重启可校验完整包；保持初始禁用状态 |
| 更新待确认 | 恢复原版本、原来源及更早的回滚历史 | 启用／禁用两种状态均保留；候选成为可回收包 |
| 更新延期到首次使用验证 | 保留候选及延期标记 | 不因重启启动代码；候选、原版本和更早历史三个版本均保留 |
| 延期验证已经开始 | 恢复原版本与原历史 | 启用／禁用两种状态均保留；不把中断的验证当成功 |
| 更新已经确认 | 保留新版本和上一版本 | 更早、已不再引用的版本可回收 |
| 手动回滚待确认 | 恢复回滚之前的当前版本与历史 | 保留原启用状态，未确认回滚不被当作完成 |
| 恢复时索引锁仍被另一进程持有 | 保留磁盘状态，拒绝写入，等待后续重试 | load 返回 busy；启停写入及删除被拒绝；释放锁后新进程恢复成功 |

更新、延期验证、确认、回滚和恢复锁占用分别测试启用及禁用状态，加上三个安装阶段，共 **15 个数据场景**。测试入口为 [TransactionCrashTests.cpp](../../tests/plugin-packages/TransactionCrashTests.cpp)，子进程为 [PackageTransactionProbe.cpp](../../tests/plugin-packages/PackageTransactionProbe.cpp)。它们链接生产包模块，没有增加生产故障开关。

每个场景先确认子进程的 PID 与原生创建时间仍存活，再强制终止同一个 QProcess，等待 CrashExit 并确认该身份退出。恢复在新建原生进程中执行，并校验当前、previous 和 pendingPrevious 的实际文件摘要。第二个恢复进程必须返回相同状态且不再改写索引；回收后第三个进程再次独立验证所有保留版本，避免引用扫描与回收代码共享错误时出现虚假通过。测试目录包含中文和空格，插件入口若被误执行会写入 marker 并抛错，实际未产生 marker。

相关三个 CTest 目标通过（6.02 秒）：包测试 48 项、中断测试 17 项、GitHub 事务 42 项，计数包含各自初始化／清理。可单独复现：

```sh
cmake --build build --target PluginTransactionCrashTests
NNP_TRANSACTION_CRASH_OUTPUT=/absolute/path/transaction-results.json \
  ctest --test-dir build --output-on-failure -R '^PluginTransactionCrashTests$'
```

这是明确事务阶段的进程中断证据，不是断电、磁盘已满、硬件 fsync 故障或原子 rename 系统调用内部终止的证明。GUI 命令发布、管理器操作与应用退出竞态，以及其他平台执行仍需各自的验收。该增量只改测试和测试 CMake 接线，上一轮签名应用的生产输入未变；不据此宣称重新生成发行包或完整计划已完成。
