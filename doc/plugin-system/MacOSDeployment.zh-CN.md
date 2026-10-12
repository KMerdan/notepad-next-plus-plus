# macOS 插件应用部署

用户已于 2026-10-11 明确当前只做本机开发，不做身份认证和发布。**当前无需 Developer ID 或 notarytool profile**。普通 CMake 编译／运行与插件测试不调用公证；本地打包默认 `--identity -`、空 `--notary-profile`，使用无需证书的 ad-hoc 签名。以下正式发行流程及历史签名结果仅作为未来参考，不属于当前开发验收，也不再要求用户配置凭据。

当前流程生成 Node 插件版应用，包含 Qt、WebEngine、受控 Node 和宿主代码。它与使用绝对 Python 解释器的开发构建分开；Python 原型仍保留在项目中。此页的本机验证不能替代 Intel、最低系统版本、干净系统与正式签名发行验收。

## 构建与介质

使用匹配架构的固定 Node 官方档案和二进制，版本及档案摘要见 `packages/plugin-host/runtime-lock.json`。Qt SDK 必须包含 WebEngine、WebChannel，以及部署后的 Qt 插件实际需要的模块。当前 Qt 6.10.0 部署中，NMEA 定位插件需要额外安装 Qt SerialPort；`macdeployqt` 返回成功本身不足以证明依赖完整。

```sh
cmake -S . -B build-macos-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/6.10.0/macos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 \
  -DNOTEPADNEXT_PLUGIN_WEBVIEWS=ON \
  -DNOTEPADNEXT_PLUGIN_NODE=/absolute/path/to/node-v24.21.0-darwin-arm64/bin/node \
  -DNOTEPADNEXT_PLUGIN_PYTHON= \
  -DNOTEPADNEXT_MACOS_NODE_ARCHIVE=/absolute/path/to/node-v24.21.0-darwin-arm64.tar.xz
cmake --build build-macos-release --target dmg -j 6
```

`dmg` 和 `plugin_dmg` 调用 `scripts/package-macos.py`。打包工具验证 Node 档案 SHA-256，并将档案内原始 Node 二进制摘要与构建包中的二进制比较，再复制应用、部署 Qt、附加 notices、由内到外签名、验证并生成 DMG。源构建包不被打包过程修改。每次结果写入 `build-macos-release/plugin-packages/NotepadNext-<arch>-<unique>/`，包含应用、部署日志、`bundle-validation.json` 和 `package-record.json`。x86_64 使用对应 x64 Node 档案和独立构建；当前入口只接受单个目标架构。

正式签名采用本机钥匙串：Developer ID Application 身份安装在本机，公证凭据通过 `xcrun notarytool store-credentials` 保存为钥匙串 profile，构建仅引用身份名称和 profile 名称。证书私钥和密码不写入仓库或测试记录。当前身份已实际签名验证，用户确认公证 profile 尚未配置。

传 `-DNOTEPADNEXT_MACOS_SIGN_IDENTITY="Developer ID Application: ..."` 可选择已配置身份。默认 `-` 是本机开发签名，记录明确标为 `ad-hoc-development`。开发签名的主程序需要允许加载分别签名、没有共同 Team ID 的 Qt 库；此例外只用于默认开发签名。正式身份的主程序不添加该 entitlement。Node 与 WebEngine helper 使用单独的 JIT profile，禁止调试和 DYLD 环境注入 entitlement。签名使用 hardened runtime；验证可使用 `--deep`，签名按嵌套顺序执行。

## 验证与边界

`scripts/verify-macos-bundle.py` 检查实际 Mach-O 架构、最低系统、依赖与 RPATH，拒绝外部依赖、越界或损坏的符号链接，验证 Cocoa 平台插件、Node 版本、WebEngine helper 和必需资源、签名与 entitlement。`--skip-signatures` 仅用于部署中间状态检查。验证结果是所选架构的应用包检查，不能由它推定其他 CPU 架构或旧系统实际可运行。

```sh
python3 scripts/verify-macos-bundle.py /absolute/path/to/NotepadNext.app --arch arm64
python3 tests/macos-packaging/test_bundle.py
```

应用集成测试通过 `NNP_DEPLOYMENT_AUDIT_PATH` 输出加载的 Qt 映像、Qt 插件目录、Node 和宿主位置。部署测试使用复制后的测试应用、带空格和中文的路径，并清理 Qt、DYLD、Node 与 Python 覆盖变量；测试不传文件名参数。测试程序包含生产 Application、MainWindow 和插件服务源代码，其插件打包与导入 fixture 仍取自开发目录。这是搬移和运行验证，干净机器安装仍需独立执行。

应用中的 `Contents/Resources/licenses` 保留当前 Qt SDK 的 SPDX 文件、提取的第三方 notices，以及基础组件许可证。Node、宿主和静态 zlib 的 notices 随宿主文件复制。此清单尚未完成所有发行组件的许可证、对应源码与再分发义务审核；`fullLicenseAndSourceGateComplete` 保持 false。

本机后续检测到一个可用的 Developer ID Application 身份，已使用本机钥匙串完成应用与 DMG 的正式签名。所有实际 Mach-O 的签名身份属于同一 Team，具有安全时间戳；主程序没有开发签名所需的库验证例外。签名 DMG 已校验、只读挂载、复制并卸载；复制后的生产程序和签名 Node JIT 运行成功，搬移后的签名集成应用通过编辑器／管理器／插件测试。记录见 [MacOSDeveloperIdValidation.json](evidence/MacOSDeveloperIdValidation.json)。早先身份数量为 0 的记录保留为历史证据。

公证、stapling、Gatekeeper 接受、实际最低系统与原生 Intel 验证，以及干净机器安装、升级与卸载仍待完成。配置 `NOTEPADNEXT_MACOS_NOTARY_PROFILE`（脚本 `--notary-profile`）会将应用 ZIP 和已签名 DMG 提交给 Apple；只在返回 Accepted 并完成 stapling 验证后记录成功。默认没有 profile，不提交。用户已确认本机 profile 尚未配置，未读取公证凭据，也未提交公证；Gatekeeper 本机评估返回 `Unnotarized Developer ID`，`notarized` 和 `cleanMachineValidated` 保持 false。[Apple 公证流程](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)、[Qt macOS 部署](https://doc.qt.io/qt-6.10/macos-deployment.html)、[Qt WebEngine 部署与 helper 资源](https://doc.qt.io/qt-6.10/qtwebengine-deploying.html)、[Node 24 平台要求](https://github.com/nodejs/node/blob/v24.x/BUILDING.md#supported-platforms)

可在本机终端交互配置，例如：

```sh
xcrun notarytool store-credentials "NotepadNext"
```

当前本机 `notarytool store-credentials --help` 明确支持省略凭据参数后交互输入。直接在本机终端完成身份、Team 与凭据提示；只向协作方提供保存后的 profile 名称，不把密码或私钥发送到聊天。profile 名称只是钥匙串凭据的引用，保存后仍需实际提交和验证，不能据保存成功宣称公证完成。

加入原生主题字体和安装锁竞争修正后，当前包已重新正式签名验证。DMG 为 308,875,428 字节，SHA-256 `d24b36fdce367bb4f2fd9e2c9cbf7fa17f9237abbbb72676f5ef857679194c0f`；36 个生产 Mach-O、签名 Node JIT 和搬移后的 7 项应用测试通过。详细路径及原始检查见 [NativeAppearanceDeveloperIdValidation.json](evidence/NativeAppearanceDeveloperIdValidation.json)，完整限定见 [NativeAppearanceValidation.json](evidence/NativeAppearanceValidation.json)。此前签名包记录保留为历史证据。
