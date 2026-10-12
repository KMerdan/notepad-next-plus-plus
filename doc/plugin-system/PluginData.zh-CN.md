# 插件配置、数据与卸载

`storage@1` 与 `configuration@1` 是独立的声明能力。Qt 管理器只授予 metadata 的 required 能力；缺少授权的调用返回 `1003 CapabilityDenied`。两者都属于当前插件，命名空间同时绑定插件 ID 与安装记录中的规范来源仓库；另一个仓库声明相同 ID 也不能通过宿主 API 读取先前保留的数据。插件不能在 params 中指定其他插件或仓库。configuration 不读取或修改主程序设置，也没有工作区、跨插件访问、路径访问或隐式默认值。

方法使用当前 sessionId/generation、对象 params 和请求确认：

| 方法 | params（除会话身份） | result |
| --- | --- | --- |
| `nnp.storage.get` / `nnp.configuration.get` | `{key}` | JSON 值；不存在返回 null |
| `nnp.storage.update` / `nnp.configuration.update` | `{key,value?,expectedValue?}` | null |

可选 expectedValue 在同一文件锁内与当前 get 值比较；不相等返回 `1001 VersionConflict` 并保持磁盘不变。expectedValue:null 匹配缺失或已存 null；不提供该字段维持原有写入行为。Node 的 compareExchange、Python 的 compare_exchange 使用这个契约。

update 省略 value 表示删除；显式 JSON null 会存为一个值。TS SDK 的 `update(key, undefined)` 使用删除语义。key 限定 1–128 个 ASCII 字符，首字符 `[A-Za-z0-9_]`，其余可包含字母、数字、下划线、点和连字符。点不表示路径或对象层级。

每个 JSON 值最多 64 KiB（紧凑 UTF-8 编码），结构嵌套最多 32 层。每个命名空间最多 256 个 key，插件两个命名空间的整个持久化记录最多 1 MiB。值／记录超限返回 `1008 ContentTooLarge`；key 数或异步队列超限返回 `1007 Overloaded`；参数不合法返回 `-32602`。磁盘读写、锁竞争或损坏数据返回 `1010 DataUnavailable`，原文件保留。锁竞争允许用户稍后重试，损坏文件不会自动被空配置覆盖。

读取允许在 Activating 或 Active；写入只允许完成验证并发布资源的 Active 会话。在 activate() 中可读取上次状态，但写入应放在激活返回后的命令或事件回调中。Activating、待确认更新、停止中和过期会话写入返回 `1004`。这样，激活失败不会修改旧版本的宿主数据。成功激活后的代码回滚不会撤销已经确认的写入，也不会自动运行迁移脚本；插件作者必须保留旧 key 的可读性，使用新 key 逐步增加数据版本。此能力没有数据库事务或备份／迁移 API。

Qt 服务按插件命名空间串行并公平派发到文件 worker，最多并行处理四个命名空间、全局接纳 128 个操作、单命名空间队列 64 个操作。JSON 记录采用 schemaVersion=1，pluginId／repository 校验、两者组合的 SHA-256 文件名、读取预算、QLockFile 和 QSaveFile 原子替换。索引与插件不可变包独立；插件代码和配置文件不位于签名后的 app 内。同一插件的读取排在较早写入之后，成功应答表示原子写入完成。取消／超时不会撤销已提交写入；连接关闭后不会把迟到结果投递给新的会话。应用正常退出等待数据队列结束。

卸载默认保留配置与数据。确认框的 “Also delete this plugin's data and configuration” 复选框默认关闭。选择删除时，先停止插件、撤销菜单／窗口、移除安装索引，再将本插件的数据删除排在已有写入之后。删除失败时插件仍已卸载，管理器显示保留数据的原因；不会删除其他插件、注册来源或任意路径。重新安装默认禁用；保留的数据在该插件下次启用时可读取。

普通 Node 插件仍拥有当前用户的操作系统权限。这些规则约束宿主 API，不能阻止插件通过 Node 文件 API 自行读取磁盘。

验证入口：`tests/plugin-data/PluginDataTests.cpp` 覆盖 Unicode JSON、命名空间／插件隔离、原子持久化、锁、损坏文件、链接拒绝、各项预算和有序删除；`PluginWebviewTests::pluginDataLifecycle` 用实际 Node 插件及 SDK 检查激活写入拒绝、重载、删除 key、保留数据重新安装和删除数据重新安装。

VS Code 兼容层在这些命名空间上提供有界 Memento 与声明配置；其单编辑会话范围、确认缓存、竞争合并和迁移限制见 [状态与配置适配](VSCodePersistence.zh-CN.md)。
