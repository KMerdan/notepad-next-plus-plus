# 统一 metadata 与资源注册入口

设计修订：2026-10-11。资源解析、持久化、Qt 管理窗口、本地／GitHub Release 安装与运行入口已接入；完整交付状态见[实施记录](Implementation.zh-CN.md)。沿用 [统一 metadata Schema](manifest.schema.json)，补充 [单插件示例](examples/markdown-preview.manifest.json) 和 [Marketplace 示例](examples/marketplace.manifest.json)。

## 用户流程

单个插件仓库与 Marketplace 仓库使用同一“注册资源”入口。注册成功都打开 **Discover**；注册本身不下载插件发行包、不安装、不授予宿主能力、不启动插件，也不创建菜单、图标或面板。用户随后选择资源，点击“安装”，才进入安装确认与既有安装事务。

本机开发也使用此入口：选择 **Local metadata file**，输入或浏览本地 `notepadnext-plugin.json`，检查后确认注册。单插件和 Marketplace 仍采用同一 Schema 与 list 分流；本地读取不访问网络。没有声明 repository 的单插件以规范化 metadata 文件 URI 作为稳定身份，且只能选择本地包安装；已声明实际 GitHub repository 的 metadata 保留其发行身份。Sources 保存本地文件路径、实际内容 SHA-256 和检查时间，不伪造 Git commit。刷新重读该文件，失败保留缓存，文件消失后重启仍可查看原资源。未发布插件无需仓库、Developer ID 或公证。移动没有发行 repository 的 metadata 文件属于来源变化，应显式处理旧来源和安装记录。

`RegisteredResourceRegistry` 保存可安装资源，`InstalledPluginRegistry` 保存实际已安装版本；两者不是同一张状态表。只有 metadata 缓存和资源记录写入成功，注册才算成功。

| 用户动作 | 资源目录 | 已安装插件与运行状态 |
| --- | --- | --- |
| 注册单插件 | 增加一个资源，进入 Discover | 不变 |
| 注册 Marketplace | 展开 list，增加多个资源，进入 Discover | 不变 |
| 选择资源并确认安装 | 保留资源及来源信息 | 成功后进入 Installed，默认禁用；启用后显示入口，首次调用激活 |
| 刷新来源后选择更新 | 原子替换有效 metadata | 保留启用状态；停止旧会话，验证新版激活，失败恢复旧版 |
| 卸载插件 | 注册来源仍有效时，资源回到 Discover | 入口、进程和安装索引移除；未引用包后台回收，用户数据可显式保留或删除 |
| 移除注册来源 | 删除此来源关系；资源还有其他来源时保留 | 已安装插件、数据与原发行更新来源不变 |

Discover 显示所有已注册资源，包括已安装的插件；安装状态以徽标和操作区分。已安装资源重复注册时保留来源关联，显示“已安装”并提供 Installed 入口，不把它当成未安装资源，也不自动升级或重装。

## 一个文件，一个分流字段

仓库根目录统一使用 `notepadnext-plugin.json`。所有 metadata 使用同一 Schema、同一 manifestVersion；本次仍为尚未发布的 v1 草案，不需要另加 kind、marketplace 标记或两套文件名。

- `list: false`：该 metadata 描述一个插件。
- `list: [ ... ]`：该 metadata 描述 Marketplace。列表成员使用相同插件 metadata，且各自 `list: false`。
- `list: []`：合法的空目录，注册成功后显示空 Discover 状态。
- 缺失 list、true、null、字符串以及未知 manifestVersion 均报明确格式错误，不猜测类型。
- 首版不嵌套 Marketplace。限制目录文件字节数、条目数量与 JSON 深度，防止递归或无限展开。

共用字段为 manifestVersion、id、name、version、description、repository、list。单插件还包含既有 engines、runtime、capabilities、activationEvents 与 contributes 字段；Marketplace 无需运行时或命令贡献。类型由 list 的 JSON 类型决定，字段内容按同一个 Schema 的条件规则验证。

直接注册插件时可将输入仓库补为其 repository；Marketplace 的每个插件条目必须声明独立的发行仓库。目录仓库 URL 与插件发行仓库 URL 分别保存。Schema 检查字段结构；SemVer、重复 ID、来源一致性、命令归属等仍需语义校验。

## 入口解析与注册职责

`SourceEntryResolver` 接受 GitHub 仓库 URL、可选 ref 与取消令牌：

1. 规范化受支持的 HTTPS 仓库 URL，拒绝含糊路径；解析仓库身份及 ref。
2. 读取统一 metadata 文件，限制大小、超时和重定向，校验 Schema 与语义。
3. 将 `list: false` 规范化为单元素资源数组，将 list 数组规范化为多个资源。结果只包含来源快照和资源描述，无安装副作用。
4. 展示注册确认；用户确认后由 `RegisteredResourceRegistry` 原子写入来源、metadata 快照及资源来源关联。
5. 打开 Discover。发行版本选择、平台兼容性与下载包验证推迟到用户主动安装时。

内部解析结果为 `{ source, resources[] }`。source 保存输入 URL、规范化仓库、ref、实际 commit、metadata 文件摘要与抓取时间；每个 resource 保存 pluginId、显示 metadata、发行仓库与来源关联。内部来源类别可以从 list 推导；外部 metadata 无需重复类型字段。

输入相同 URL 重复注册采用幂等 upsert。相同 pluginId 且同一发行仓库的资源合并来源关系；相同 pluginId 指向不同发行仓库时标记来源冲突，要求显式选择，不静默覆盖当前已安装插件或更新来源。初次注册有坏条目或重复冲突时，不部分写入；修复 metadata 后重试。

注册入口不启动 PluginProcess，也不调用运行时 JSON-RPC。它属于 Qt/C++ 的资源发现与登记服务，插件执行仍使用原来的 JSON-RPC 协议。

## 资源目录与安装器的连接

Discover 中的“安装”将资源的 pluginId、发行仓库及注册 provenance 传给现有 `PluginInstaller`。安装器选择兼容发行包，下载到 staging，读取实际包内同一格式的 metadata，并要求 `list: false`。Marketplace metadata 不能作为可执行插件包安装。

实际包内规范化 metadata 必须与注册候选一致，包括 pluginId、版本、发行仓库、能力、入口和贡献；安装器还独立检查实际文件、平台/API 兼容性与完整性。目录缓存不能取代包验证。成功安装后才提交安装记录，启用后注册宿主入口；失败保留 Discover 资源并显示错误，可重试。

安装、更新、回滚、启停与卸载复用 [主技术方案](../PluginSystemPlan.zh-CN.md#63-安装与更新)。Discover 是目录视图，Installed 是本机安装视图；启用、进程运行、面板打开仍是分别管理的维度。

## 来源刷新与移除

Sources 同时管理直接插件来源与 Marketplace 来源：查看 metadata、手动刷新、移除注册。刷新重新读取同一文件；校验成功后原子替换快照并更新资源来源关系。新目录移除某个插件条目时，只撤销该来源关联，不卸载已安装插件。

刷新失败保留最后有效快照，并显示失败原因与缓存时间。注册离线且没有有效缓存时不建立成功记录。404、无 metadata、格式错误、未知版本、超时和限流分别反馈；用户可以取消并保留输入。

移除来源前说明未安装资源的变化；已安装插件保存独立的发行仓库与注册 provenance 快照。Check Updates 可以独立读取已安装插件原发行仓库的新版 metadata；来源移除后仍可检查和更新，不会自动重新注册目录。原发行信息仍保留，不能被其他同名资源静默替换。来源迁移单独确认。卸载后的插件，只有仍有注册来源时，才回到 Discover。

## 实施边界与验收

拟新增 `SourceEntryResolver`、`RegisteredResourceRegistry` 与管理器的 Discover / Installed / Sources 视图。原 PluginInstaller 不再承担注册入口的类型判断，只负责用户显式选择的安装候选。将此能力并入 I2，保留资源发现与安装事务之间的可测试边界。

验收必须证明：

- 单插件与 Marketplace 注册均只写资源记录，包下载次数、安装记录写入次数、启动进程数和宿主入口增量均为零。
- 两种来源进入同一 Discover 视图；显式安装成功后转入 Installed，卸载后按注册关系回到 Discover。
- 缺失/非法 list、嵌套目录、重复 ID、来源冲突和未知版本都不产生部分注册。
- 重复注册、取消、中断和恢复具有幂等性；目录刷新失败不丢弃有效缓存。
- 移除来源不停止或卸载已安装插件；更新仍使用原发行仓库。
- 包内 Marketplace metadata 被安装器拒绝；实际插件包通过既有兼容性、授权与文件检查。

[Figma 注册资源草案](https://www.figma.com/design/cJPphvdJ1ztsU0tzMFIBpD?node-id=23-8653) 展示该契约；示例资源与仓库不代表真实发布或已安装插件。
