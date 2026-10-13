# Notepad Next ++

An independently maintained fork of [NotepadNext](https://github.com/dail8859/NotepadNext), by KMerdan. A cross-platform Qt/Scintilla text editor with macOS UI improvements and a native plugin SDK for extending existing editing features.

NotepadNext's original copyright notices are retained. This fork continues under **GPL-3.0-or-later**; see [LICENSE](LICENSE). This project and its Marketplace are maintained for this fork and are not official upstream projects.

## Start here

| I want to… | Start with |
| --- | --- |
| Build the editor | [Build instructions](doc/Building.md), [plugin-enabled build](doc/plugin-system/Implementation.zh-CN.md) |
| Install Markdown or HTML Preview | [Official Marketplace](https://github.com/KMerdan/notepadnext-markdown-preview), [manager lifecycle](doc/plugin-system/OfficialMarketplace.zh-CN.md) |
| Develop a plugin | [Independent SDK](https://github.com/KMerdan/notepadnext-plugin-sdk), [native API 0.3](packages/plugin-sdk/docs/PhaseB.zh-CN.md), [API 0.2](packages/plugin-sdk/docs/NativeAPI.zh-CN.md) |
| Understand current capability and limits | [Phase B implementation](doc/plugin-system/NativeSDKPhaseBStatus.zh-CN.md), [Phase A acceptance](doc/plugin-system/NativeSDKStatus.zh-CN.md) |
| Plan the next native APIs | [SDK Phase B priorities and acceptance plan](doc/plugin-system/NativeSDKPhaseBPlan.zh-CN.md) |
| Understand architecture and future APIs | [Native SDK design](doc/plugin-system/NativeSDKDesign.zh-CN.md) |
| Find technical documentation | [Documentation index](doc/plugin-system/README.md) |

## Three repositories

| Repository | Owns |
| --- | --- |
| [notepad-next-plus-plus](https://github.com/KMerdan/notepad-next-plus-plus) | Native editor services, Qt UI, JSON-RPC host, plugin install/update/lifecycle and native tests |
| [notepadnext-plugin-sdk](https://github.com/KMerdan/notepadnext-plugin-sdk) | API types, metadata/wire contracts, templates, debugging/testing/service helpers and packaging tools |
| [Official Plugin Marketplace](https://github.com/KMerdan/notepadnext-markdown-preview) | Markdown/HTML previews, Text Inspector, TODO Navigator, catalog and independent installable releases |

The SDK is a Git submodule pinned to a reviewed commit. The Marketplace uses locked, immutable SDK tarballs: published Releases for existing previews and a pinned commit archive for the Phase B development plugins. Its original repository URL is retained so existing Markdown installations keep their trusted source and data. SDK 0.3 and the new plugin Releases are separate from these development branches.

## Build and validate

Build the default branch, including its pinned dependencies:

```sh
git clone --recurse-submodules https://github.com/KMerdan/notepad-next-plus-plus.git
cd notepad-next-plus-plus
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --parallel
```

Core plugin support is enabled by default. Preview panels require Qt WebEngine/WebChannel and `-DNOTEPADNEXT_PLUGIN_WEBVIEWS=ON`. Bundle the pinned Node 24 binary using `-DNOTEPADNEXT_PLUGIN_NODE=/absolute/path/to/node`; [runtime preparation](doc/plugin-system/CrossPlatformCI.zh-CN.md) verifies the controlled archive. Set `-DNOTEPADNEXT_PLUGINS=OFF` for an editor-only build. Local macOS development requires no Developer ID or notarization.

For contract/integration tests, configure `-DNOTEPADNEXT_BUILD_PLUGIN_TESTS=ON` and the explicit Node test runtime. Run `ctest --test-dir build --output-on-failure`. See the [conformance workflow](.github/workflows/plugin-conformance.yml) for WebEngine, core-only and disabled-plugin configurations. Local macOS verification does not establish released support on every platform.

## Extend the editor

Native API 0.3 extends the 0.2 document/selection foundation with bounded range reads, native subscriptions, file lifecycle, literal search, language/EOL/bookmarks and owned highlights. The shared native command catalog now contains 18 operations; API 0.2 retains its original eight. Menus and plugins reuse native services. Full HTML panels use `acquireNotepadNextApi()`; panels retain generic hot-update restoration, and new-session highlights publish only after validation. See the [implementation and limits](doc/plugin-system/NativeSDKPhaseBStatus.zh-CN.md).

Existing API 0.1 Markdown/HTML packages keep their callback contract and IDs. Register a repository through **Plugin Manager → Add Source…**; then explicitly install from **Discover**, enable in **Installed**, and open its command. Updates restore owned panels; uninstall is separate from removing a source.

The SDK's purpose is to enhance Notepad Next ++. Other editors' plugins may be studied and ported. A retained bounded VS Code adapter/importer is a historical experiment, isolated from the native SDK. Providers, encoding conversion, macros and other conditional extensions remain future work.

Trusted Node/Python plugins run in separate processes. Capabilities govern host RPC; they do not provide an operating-system sandbox. The Python prototype currently negotiates API 0.1 and needs an explicitly configured Python 3.12 interpreter.

## License and upstream

[GPL-3.0-or-later](LICENSE). Based on [dail8859/NotepadNext](https://github.com/dail8859/NotepadNext), copyright Justin Dailey and contributors. Fork modifications are maintained by KMerdan. Original notices and third-party licenses remain intact. Upstream build names such as the `NotepadNext` CMake target are retained; displayed product/executable are Notepad Next ++ / NotepadNextPlusPlus.
