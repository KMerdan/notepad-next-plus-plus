# 插件增量依赖与发行记录

此文件记录新增插件功能涉及的依赖；基础编辑器依赖仍由项目现有构建／发行流程管理。当前本机验证不是正式跨平台发行清单。

| 组件 | 使用方式与固定信息 | 许可／资源处理 |
| --- | --- | --- |
| Node | 独立插件进程；版本、平台档案与 SHA-256 见 `packages/plugin-host/runtime-lock.json` | 供应者先验证官方档案；CMake 校验二进制版本，并复制档案自带完整 LICENSE 为 `plugin-host/NODE-LICENSE` |
| Qt WebEngine／WebChannel | 可选预览模块；完整 HTML 兼容 API 要求 6.6+，本机验证 6.10.0 | 需要随最终发行携带 Qt 模块与第三方 notices；helper、资源、最低系统与干净系统部署尚需发行验收 |
| zlib | ZIP 的 raw Deflate 和 CRC；优先系统库，缺失或指定 `NOTEPADNEXT_BUNDLED_ZLIB=ON` 时构建固定 1.3.2 静态库 | 官方 source archive SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`；原始 notice 保存在 `licenses/zlib-1.3.2-LICENSE`，静态 fallback 发行复制为 `plugin-host/ZLIB-LICENSE` |
| Node 宿主与 TS SDK | 项目内自建 JSON-RPC 与客户端，没有新增 npm RPC 运行依赖 | `packages/plugin-host/LICENSE` 与 SDK LICENSE 沿用 GPL-3.0-or-later；宿主 notice 随构建复制 |
| Python 原型 | 明确配置的 Python 3.12 路径；本机测试 3.12.13；项目适配层仅使用标准库 | 适配层 GPL-3.0-or-later；解释器尚未随包分发，正式版本锁、Python notices、部署与维护待验收 |
| Markdown 示例 | TypeScript 构建；renderer 与依赖由示例 package-lock 固定 | SDK pack 工具复制生产依赖原有许可证并输出 `third-party-dependencies.json`；不把开发依赖和构建 hooks 带入发行包 |

2026-10-11 核对[官方发行页面](https://zlib.net/)：当前版本为 1.3.2，档案摘要与官网一致；固定静态 fallback 的包测试与完整应用构建已通过，原始 notice 随应用复制。离线构建可传 `-DNOTEPADNEXT_ZLIB_ARCHIVE=/absolute/path/to/zlib-1.3.2.tar.gz`，CMake 仍验证固定 SHA-256。插件关闭的基础编辑器不进入这一依赖路径。

当前安装器接受 JS 和 Python metadata／发行包，拒绝 `.node` 原生 addon；不在用户机器运行 npm、pip、源码构建或 postinstall。Python 激活要求开发构建配置解释器，见 [Python 适配](PythonAdapter.zh-CN.md)。增加可分发解释器、原生扩展或其他运行时前，应分别补架构、许可、部署和维护证据。

正式发行还需汇总实际链接的 Qt／系统组件版本与 notices、验证 relocatable 包、签名／公证／安装介质和运行时升级流程。原生能力授权限制宿主 API；普通 Node 进程继续拥有当前用户的操作系统访问权限。

2026-10-11 新增 [macOS Node 版部署流程](MacOSDeployment.zh-CN.md)：Qt 6.10.0、Node 24.21.0、arm64 Release 的独立应用包完成本机依赖／签名、搬移运行和开发 DMG 验证。包内保留当前 SDK 的 11 份 SPDX 文件及其提取 notices，并附基础编辑器和运行时许可证。清单不等于完整发行许可审核；对应源码、完整依赖义务与其他平台仍保留为发行门槛。
