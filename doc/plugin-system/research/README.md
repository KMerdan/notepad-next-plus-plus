# JSON-RPC 库验证记录

研究日期：2026-10-10。此目录保存技术选型证据，尚未接入 NotepadNext 的运行时代码。

## 结论与适用范围

针对 [JSON-RPC 2.0 标准](https://www.jsonrpc.org/specification)，运行了 15 项请求封装、通知、错误和批量分派探针。结果保存在 [rpc-conformance-results.json](rpc-conformance-results.json)，脚本为 [rpc-conformance.mjs](rpc-conformance.mjs)。

| npm 包及版本 | 接入方式 | 通过数 | 未通过项目 |
| --- | --- | --- | --- |
| `json-rpc-2.0@1.8.1` | 原始 `JSONRPCServer.receiveJSON` | 13/15 | 无 id 且 method 类型错误的非法请求未返回错误；请求与通知混合的批量调用只剩一个响应时返回对象，而不是数组 |
| `vscode-jsonrpc@9.0.3` | 官方流读写器和 `createMessageConnection` | 6/15 | null id 请求、非法 JSON、非法标量、非法 method 类型和全部五类 batch 用例 |
| `json-rpc-2.0@1.8.1` | 研究用请求适配层 | 15/15 | 本组没有失败 |

第三行只证明：在进入库之前校验请求、逐项分派批量调用、保持批量响应数组，可以修复这两项已观察到的行为。适配层只处理入站请求，缺少响应校验、生产帧解析、取消、超时、背压和 Qt 集成；它不是可交付的 RPC 实现。

`vscode-jsonrpc` 的观察窗口为每例 100 ms，使用内存 `PassThrough` 流，不是独立操作系统进程。未响应与库的诊断一起记录。这项结果用于识别与本项目要求的差异，不是所有版本、所有接入方式的全面评价，也不是性能排名。当前脚本没有检查 JSON-RPC 的全部规则。

生产端点应运行同一套扩充后的共享契约用例，不能根据库名或 README 宣称完全符合标准。候选库一手资料：[json-rpc-2.0 仓库](https://github.com/shogowada/json-rpc-2.0)、[Microsoft vscode-jsonrpc 仓库](https://github.com/microsoft/vscode-languageserver-node/tree/main/jsonrpc)。

## 环境与复现

本次实际运行于 macOS、Node `v23.11.1`。这是现有开发环境，不是推荐的生产版本；[Node 官方发布表](https://nodejs.org/en/about/previous-releases)标明 Node 23 已 EOL。技术方案建议随软件提供固定补丁版本的 Node 24 LTS，实施前必须在该版本和目标系统上重跑。

在仓库根目录执行以下命令。依赖装到临时目录，不修改应用的依赖。`mktemp` 的模板适用于本次 macOS 环境。

```sh
rpc_probe_dir=$(mktemp -d /private/tmp/notepadnext-rpc-probe.XXXXXX)
npm install --prefix "$rpc_probe_dir" --ignore-scripts --no-audit --no-fund --save-exact json-rpc-2.0@1.8.1 vscode-jsonrpc@9.0.3
node doc/plugin-system/research/rpc-conformance.mjs "$rpc_probe_dir" > "$rpc_probe_dir/results.json"
```

本次 npm 元数据返回的发行包完整性值如下。版本是研究快照；正式选型需要保留 lockfile 并审核实际采用的源码、许可证和依赖。

| 包 | `dist.integrity` |
| --- | --- |
| `json-rpc-2.0@1.8.1` | `sha512-BmN42JKn6FI36pcMMzHCXQhpKNEfbeqgSIcUYHKnifq0/4Ey6PDinovwUOgajT7NsgUtpWdDXaCWlpI4LHXN3g==` |
| `vscode-jsonrpc@9.0.3` | `sha512-qETYf9PCl1lVfwR+oBmMfc4vP2nqLNA35ABxorw3j06+xlKRgDVxbLgKm4Y/xPL0YWdeDjLoI4ImICIT3nB0zQ==` |

## 尚需验证

补充无效 params、id 类型、成功和失败响应互斥、响应 id 类型保持、重复及迟到响应、批量乱序、分片 UTF-8、超长及重复头、流截断、关闭和反向请求。Qt 与 Node 必须共同运行这些用例；只测试 TS 服务端不足以验证双向协议。

此目录上一级的 Manifest Schema 是首版原生 Node 插件的结构草案，使用 [JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12/json-schema-core)，包含命令、插件菜单、工具栏与包内 PNG 图标声明。它不会检查 SemVer 范围、命令归属、menu/toolbar 引用关系、能力授权、入口/图标文件存在性或真实路径边界，也不代表 VS Code 插件的完整 Manifest。菜单/图标修订经过 9 项结构检查：有效完整示例、仅命令示例，以及绝对图标路径、越界路径、远程图标 URL、不支持的菜单位置、非法排序值、缺少命令引用、重复菜单项的拒绝。
