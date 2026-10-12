# 插件平台验收矩阵

当前为本机开发阶段，用户不要求身份认证或发布。表中的正式签名、公证、干净机器发行与安装介质检查保留为未来发行事项，不再作为当前开发阻塞；本机功能和构建证据独立使用。

基础编辑器的 CI 构建矩阵与插件发行验收分开记录。`.github/workflows/build.yml` 目前安装 Qt Core5Compat，没有启用 WebEngine、配置受控 Node 或调用插件部署验证，因此基础构建通过不能证明插件可安装、激活或打开面板。

新增 [插件专用 CI](CrossPlatformCI.zh-CN.md) 定义 Linux x64、Windows x64、macOS arm64／Intel 的 WebEngine、插件核心及插件关闭共 12 个组合，固定运行时并保存测试证据。静态检查和运行时准备已经验证；远端尚未运行，以下原生平台缺口继续保留。

| 平台／配置 | 当前证据 | 尚需验收 |
| --- | --- | --- |
| 本机 macOS 27.0.1 arm64，Qt 6.10.0，Release，Node 24.21.0，WebEngine | 20 项 Release 回归；1／5／10 个 Markdown 实例性能预算通过；100 次循环资源计数；Developer ID 应用与 DMG 签名、时间戳、搬移集成运行、包内 Qt／宿主／Node 路径及 JIT 检查 | 公证、stapling、Gatekeeper 接受、干净机器安装／升级／卸载；进一步故障与性能场景 |
| 同一 Mac，Debug，明确配置 Python 3.12.13 | 生产 Application 集成测试包含 Python 读取未保存文本、独立启停与卸载；开发构建仍保留 | Python 解释器随包分发、维护与架构／许可验证；当前 Node 发行包不包括可用解释器 |
| 同一 Mac，插件关闭 | 已编译并运行基础编辑器集成回归 | 其他系统与 Qt 版本的基础编辑器回归 |
| 同一 Mac，Release，插件核心开启／WebEngine 关闭，Node／Python 开发解释器 | 独立构建完整 22／22 CTest 通过，核心协议、文档、安装／恢复、数据与实际核心应用测试 | 其他平台／Qt 配置；此配置没有原生 WebEngine 面板验收 |
| macOS 13.5 arm64 | Node 发行档案锁、应用 Info.plist 和所有所选架构 Mach-O 的最低系统声明已检查 | 实际在 macOS 13.5 机器运行；声明检查不等于旧系统运行验收 |
| macOS Intel x86_64 | 打包入口接受独立 x86_64 构建与固定 x64 Node 档案；现有 CI 有 Intel 基础编辑器条目 | 原生 Intel 插件编译、运行、进程回收、helper 与资源部署、正式发行和最低系统验收 |
| Windows x64／arm64 | 固定 Node 档案记录和平台进程实现；现有 CI 有基础编辑器条目 | 原生插件构建／运行、Job Object、运行时与 Qt/WebEngine 部署、安装介质和干净机器验收 |
| Linux | 固定 Node 档案记录和平台进程实现；现有 CI 有基础编辑器条目 | 原生插件构建／运行、进程监督、Qt/WebEngine 沙箱及依赖部署、安装介质和干净机器验收 |
| 其他 Qt 版本与 universal macOS 包 | 现有基础编辑器矩阵含 Qt 6.5／6.8／6.10；当前插件 DMG 入口只接受单个目标架构 | 逐组合验证宿主能力；完整 HTML Webview API 已文档化为 Qt 6.6+；universal 运行时选择与双架构部署另行验收 |

当前受控 Node 的 macOS 目标下限为 13.5；最终发行仍取 Node、Qt/WebEngine、应用与插件依赖要求的交集。纯 JS 插件包的可复用性不代表 Node、原生依赖或 WebEngine helper 的跨架构可复用性。安装器目前拒绝原生 `.node` addon。

当前源码证据见 [NativeAppearanceValidation.json](evidence/NativeAppearanceValidation.json)：20／20 Release 回归、Python 开发和插件关闭的独立应用测试、原生主题字体、100 次生命周期、独立性能复测及当前 Developer ID 包的搬移运行均通过。当前 100KiB 的 1／5／10 插件 p95 为 315.4／315.9／349.0ms，最大 GUI dispatch 15.9ms；1MiB 和高 RSS 限定保留。用户确认公证 profile 尚未配置，公证与干净机器各项仍待执行。

后续测试增量补充了 [15 个包事务进程中断场景](evidence/TransactionRecoveryValidation.json) 和 [5 个生产源码应用退出场景](evidence/ApplicationExitValidation.json)。后者覆盖无响应插件、排队重载／卸载和候选更新，12 个所属 Node／子进程在 exec 返回时退出；Python 开发和插件关闭的应用也重新编译验证。增量只改测试和接线，生产包输入未变；不扩大本机证据的平台范围。

最新 [六场景应用退出记录](evidence/ApplicationPreparationExitValidation.json) 还覆盖实际包存储锁保持期间取消更新准备，14 个所属 Node／子进程在 exec 返回时退出，原版本、启用状态和数据保留。

此前证据保留在 [MarkdownPerformanceValidation.json](evidence/MarkdownPerformanceValidation.json)、[MacOSDeveloperIdValidation.json](evidence/MacOSDeveloperIdValidation.json) 和 [MacOSDeploymentValidation.json](evidence/MacOSDeploymentValidation.json)；部署入口和正式签名配置见 [macOS 部署说明](MacOSDeployment.zh-CN.md)。性能是所记录硬件和文档 fixture 的观测，不推定其他机器、实际像素显示时间或峰值内存。此矩阵保留原计划所有平台要求，不把未运行的组合标为支持完成。
