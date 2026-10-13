# Plugin 生命周期与 Notepad Next ++ fork

本轮更新：2026-10-12。独立维护仓库：[KMerdan/notepad-next-plus-plus](https://github.com/KMerdan/notepad-next-plus-plus)。继承来源：[dail8859/NotepadNext](https://github.com/dail8859/NotepadNext)，本机开发基线为 `adfd128`。远端的上游 `master` 保留；本 fork 的开发默认分支为 `main`。本机工作分支为 `codex/notepad-next-plus-plus`，`origin` 指向本 fork，`upstream` 指向原项目。

## 生命周期：目录、安装与运行分别管理

旧的 Uninstalled 将目录误表达成安装队列，安装后条目消失，重装和查看来源也变得难找。改为 **Discover**，明确“资源存在”和“本机安装”是独立关系。

| 入口 | 负责内容 | 主要操作 |
| --- | --- | --- |
| Discover | 注册来源展开的全部插件，含已安装项 | 未安装项提供 Install；已安装项提供 Manage Installed Plugin |
| Installed | 本机包、启用状态、版本、运行状态 | 启用/禁用、打开、检查更新、更新、卸载；More 提供日志与恢复旧版本 |
| Sources | 单插件或 marketplace 的 metadata 注册 | Add Source、刷新、检查 metadata、Remove Source |

注册只读取和确认 metadata。安装验证发布包后默认为禁用。启用注册命令，但不立即创建子进程；首次执行命令才激活。禁用关闭进程及面板，保留包和数据。更新保留启用状态与数据，失败激活恢复旧版本；热更新保留宿主拥有的面板，通过 webviews.restore@1 恢复回调重建新会话，自动恢复位置、状态、滚动和表单；不重放普通命令。恢复失败保留旧版。详情见 SDK HotUpdate 文档。

卸载默认保留数据与设置；确认窗口有显式“同时删除”选项，默认未选中。来源仍注册时，Discover 条目持续存在并恢复 Install 操作。删除来源只移除目录关联，保留已安装包、运行配置和用户数据。Installed 的 Check Updates 可以独立读取原发行仓库，不自动重新注册来源。追踪固定 tag 的来源仍按该 tag 检查；跟随新版请使用默认分支来源。

Refresh Catalog 顺序刷新所有来源；Sources 的 Refresh Metadata 仅刷新当前项。单个来源不可用时保留其上一次有效缓存，其他来源继续刷新。刷新过程可取消，并禁止重叠安装/更新操作。

## 图标与窗口

Markdown Preview 在 Discover、Installed、安装与更新确认以及主工具栏统一使用 Markdown 图标。根 metadata 可声明 `displayIcon: "markdown"`；旧发布包命令中的 `markdown-preview.png` 标识也兼容。注册阶段使用宿主内置矢量图标，不读取未验证插件目录里的文件。普通插件可在安装验证之后使用包内自定义图标。

注册入口、注册确认、安装、更新、卸载、删除来源、恢复旧版本、metadata 与日志窗口复用共享外观模块。SF Pro 系统界面字体，20/16/13/12/11 px 层级，8 px 组内、16 px 组间。长来源 URL 可换行、选取；确认按钮固定在滚动内容外。破坏性操作使用红色按钮，Cancel 为默认焦点。Light/Dark 共用语义颜色；危险按钮颜色和文字对比已记录于 `evidence/PluginLifecycleContrast.json`。

Figma：[生命周期与确认窗口](https://www.figma.com/design/cJPphvdJ1ztsU0tzMFIBpD?node-id=65-12291)、[完整管理器](https://www.figma.com/design/cJPphvdJ1ztsU0tzMFIBpD?node-id=47-12293)。旧探索稿保留，当前稿以 Discover 命名。节点证据见 `figma-plugin-lifecycle-v3*.json`。与实际 Qt 控件使用相同组件层级、文字、主动作和状态；原生文件选择器仍使用平台自身界面。

## 实际验证

实际应用集成测试通过 manager 的按钮和确认窗口操作 [Markdown 仓库](https://github.com/KMerdan/notepadnext-markdown-preview)，下载真实发布 ZIP；未伪造安装包、版本或网络响应。

1. 注册 `v0.1.2`，确认未安装、未启动子进程。
2. 安装 0.1.2，确认禁用状态、目录条目仍在。
3. 启用并打开未保存的 Unicode Markdown，检查真实 WebEngine DOM。
4. 注册默认分支，更新至 0.1.3，检查不同包摘要、启用状态和存储哨兵数据；重新打开预览，检查正文与主窗口原生句柄。
5. 禁用/再启用，确认面板与子进程清理、延迟启动。
6. 移除两个来源，确认本机插件保留；独立 Check Updates 正常且不重新注册。
7. 重新注册、取消卸载，再执行默认保留数据的卸载；确认包与面板移除、数据保留、Discover 可再次安装。
8. 取消再次安装，确认没有包或子进程产生。

还覆盖多本地来源刷新与失败缓存、100 次 Webview 生命周期清理、退出期间的挂起更新/卸载/停用、插件关闭构建以及 SDK 打包校验。最终日志见 `evidence/plugin-lifecycle-*.log`。截图由真实 Qt 窗口生成，见 `screenshots/lifecycle-*.png`。

**Not verified**：本轮未进行 Intel macOS、Windows/Linux 运行和视觉回归，VoiceOver 全流程、所有翻译以及系统最大文字放大。当前开发不涉及正式 macOS 签名、公证或二进制发布。

## Fork 名称与许可证

显示名称 **Notepad Next ++**，可执行程序与 macOS bundle 为 `NotepadNextPlusPlus`，bundle ID 为 `io.github.kmerdan.NotepadNextPlusPlus`。Qt organization 为 KMerdan，使用独立的配置与插件数据目录；旧应用和旧配置可以共存，没有自动迁移。内部 CMake 目标和 `notepadnext-plugin.json`、JSON-RPC API、SDK 名称保留兼容性。

README 与 About 标明：原项目由 Justin Dailey 与贡献者创建，本 fork 由 KMerdan 独立维护，2026 年 10 月开始修改。原始版权、无担保声明与 LICENSE 保留，整体沿用源文件指定的 GPL-3.0-or-later。第三方组件继续保留各自许可。依据 [GNU 许可使用指南](https://www.gnu.org/licenses/gpl-howto.en.html)，复制同许可代码时保留原版权声明。相关改动日期与独立维护声明不会暗示上游为本 fork 背书。

更新地址只指向本 fork；不会下载原项目的二进制替换 fork。继承的 upstream 发布后 WinGet/Homebrew/Flatpak 自动化限制为上游仓库运行，避免本 fork 的未来发布改动上游渠道。Windows/Linux 名称与打包路径已同步，但本轮没有执行这些平台的发行打包。

## 复核中修复的退场问题

窗口退场时，macOS 仍可能发送 ApplicationActive。原应用状态回调直接调用不受 QObject 生命周期保护的主窗口指针；崩溃报告定位到 QWidget::isActiveWindow。现在改用 QPointer 自动跟随窗口生命周期，再检查窗口是否存在，并在真实最后窗口关闭测试中再发送激活信号回归。此修复覆盖退场边界；不把其他等待或焦点问题都归因于该机制。

审视使用 better-colors、better-layout、better-typography：注册字段从居中的短输入改为统一左边缘；仓库事实行可换行；主确认操作在滚动内容外固定；浅深色危险按钮对比为 6.536 / 9.601，达到正文 4.5 阈值。实测截图检查上述窗口的阅读顺序与主次动作。**Approve** 仅适用于实测的 macOS 窗口；未验证项仍按上文记录。

线上重跑因匿名 GitHub API 限额暂停过。最终测试客户端可从进程环境读取既有登录凭据，仅给 api.github.com 的 HTTPS 请求附加认证；初始化后立即清除环境变量，插件子进程不会继承凭据。生产构建默认继续使用公开仓库匿名请求，未新增登录或凭据保存界面。

真实开发应用还通过 CUA 复核本地 metadata 注册、确认安装、Installed 状态和延迟启用。新 fork 配置中保留公开 Markdown 0.1.3 的本地开发包，供继续体验。安装结束后管理器会回到前台展示结果；切换本地/仓库来源时确认按钮的可访问名称跟随更新。
