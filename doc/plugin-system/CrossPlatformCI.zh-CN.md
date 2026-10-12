# 插件跨平台 CI

用户已明确当前只做本机开发，不要求身份认证或发布。此工作流是开发质量检查入口；远端运行和发行事项不阻塞本机插件功能开发，也不使用 Developer ID 或 notarytool。

[plugin-conformance.yml](../../.github/workflows/plugin-conformance.yml) 是应用仓库的插件验收入口。它在相关 PR、master 更新或手动触发时运行，保持只读仓库权限，不发布安装包、不签名或公证。原有基础编辑器发行工作流继续保留。

工作流定义 Qt 6.10.0 的 12 个组合：

| 原生运行目标 | GitHub runner | 验收配置 |
| --- | --- | --- |
| Linux x64 | ubuntu-24.04 | WebEngine／插件核心／插件关闭 |
| Windows x64 | windows-2022 | WebEngine／插件核心／插件关闭 |
| macOS arm64 | macos-15 | WebEngine／插件核心／插件关闭 |
| macOS Intel x64 | macos-15-intel | WebEngine／插件核心／插件关闭 |

上述 runner 的系统与 CPU 对应关系已核查 [GitHub 官方 runner 清单](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)。Qt Action 的平台、架构、模块和缓存参数依据其[官方文档](https://github.com/jurplel/install-qt-action)。Actions 均引用实际查询的完整 commit SHA。

三个配置分别设置 `NOTEPADNEXT_PLUGINS`／`NOTEPADNEXT_PLUGIN_WEBVIEWS` 为 ON／ON、ON／OFF、OFF／OFF。固定 Node 同时供宿主和测试使用；明确配置 Python 3.12 开发解释器供第二语言测试使用。此解释器配置不构成可搬移的 Python 发行包。Mac 使用独立架构和 13.5 deployment target；没有推定最低系统运行验收已完成。

主仓只使用 [宿主自有 contract fixtures](../../tests/fixtures/README.md)：固定的可执行测试插件直接验证命令、文档、面板恢复、安装／启用／卸载和退出；无需 npm 安装、TypeScript 编译、Markdown 渲染或 SDK 打包。包目录和 ZIP 验证使用 PackageStore／ZipExtractor 的独立测试数据。随后编译宿主测试目标并串行运行 CTest。Linux 使用 Xvfb 提供显示，工作流保持 WebEngine sandbox。应用退出目标、Python 原型、Node／Qt 协议、真实编辑器和生命周期测试按照各配置的实际 CMake 接线执行；Mac 独立性能测试仍是另行执行的参考机验收，不放入共享 runner 的性能比较。

测试归属：应用仓库验证 wire/API、能力权限、资源隔离、生命周期和原生 UI；SDK 仓库验证类型、模板、调试／打包／Release 工具；Marketplace 验证 Markdown／HTML 插件自身的构建、渲染和发布包。主仓保留 SDK schema／wire 的一致性检查，不重复执行 SDK 工具产品测试。真实插件的跨仓验收采用显式提供的预构建资产／repo URL，默认不影响主仓 PR。

即使失败也保存 CTest 原始日志、JUnit、退出进程记录、CMakeCache 和 runtime 记录，文件保留 14 天。需要检查实际 run 的结论、跳过项和这些产物，不能由 YAML 存在或静态检查通过宣称平台已通过。

## 固定运行时准备

[fetch-plugin-node.py](../../scripts/fetch-plugin-node.py) 按仓库 `runtime-lock.json` 获取官方档案，验证完整 SHA-256，再仅提取指定 Node 二进制和 LICENSE；不执行提取的程序或包脚本。档案及所提取二进制的摘要、绝对路径写入 `runtime.json`。ZIP 与 tar.xz 保留符合现有 CMake 的许可证位置；既有输出目录不会被覆盖。

```sh
python3 scripts/fetch-plugin-node.py \
  --platform darwin --arch arm64 --output /absolute/path/new-runtime-directory
```

已有固定档案可增加 `--archive /absolute/path/node-v24.21.0-darwin-arm64.tar.xz` 离线验证。Linux 使用 `--platform linux`，Windows 使用 `--platform win`；架构取 `x64` 或 `arm64`。只接受锁文件已有的组合。为其他平台提取的二进制不在本机执行。

工具包含下载和成员大小预算，拒绝校验不符、重复必需成员、非普通文件及成员大小不符。网络仅从 Node 官方 HTTPS origin 获取，重定向不能离开该 origin；摘要仍是安装之前的信任门。setup-node 仅提供测试脚本解释器；正式宿主使用校验后明确配置的 Node，不从 PATH 解析。

## 当前证据与未完成项

工作流已通过固定版本 actionlint 的静态验证。运行时工具的拒绝与布局测试通过，且实际 macOS arm64、Linux x64、Windows x64 的官方锁定档案已经校验和提取；仅 macOS 二进制在本机执行检查版本。这不构成 Linux／Windows 原生应用运行证据。

本机已完成相同插件核心配置的独立 Release 构建，关闭 WebEngine 并明确提供 Node／Python，完整 **22／22 CTest 通过（45.39 秒）**。本机没有 Ninja，改用 Unix Makefiles；初次配置诊断保留在[验收记录](evidence/PluginCIValidation.json)。该配置中应用的 WebEngine 专用测试体没有编译，不据其 QtTest 行数推定面板验收。这段为最初的本机验收快照，不能代表当前 PR 的 CI 状态。当前通过与否需读取对应提交的 GitHub checks。

原审计继续追踪实际 Windows／Linux／Intel 运行、其他 Qt 与 arm64 组合、干净系统、最低 OS、正式签名与公证、安装介质和完整许可证／对应源码义务。此工作流提供获取跨平台证据的具体入口，不缩减这些要求。
