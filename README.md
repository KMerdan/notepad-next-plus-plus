# Notepad Next ++

An independently maintained fork of [NotepadNext](https://github.com/dail8859/NotepadNext), by KMerdan. A cross-platform Qt/Scintilla text editor with macOS UI improvements and a native plugin SDK for extending existing editing features.

NotepadNext's original copyright notices are retained. This fork continues under **GPL-3.0-or-later**; see [LICENSE](LICENSE). This project and its Marketplace are maintained for this fork and are not official upstream projects.

## Start here

| I want to… | Start with |
| --- | --- |
| Build the editor | [Build instructions](doc/Building.md), [plugin-enabled build](doc/plugin-system/Implementation.zh-CN.md) |
| Install Markdown or HTML Preview | [Official Marketplace](https://github.com/KMerdan/notepadnext-markdown-preview), [manager lifecycle](doc/plugin-system/OfficialMarketplace.zh-CN.md) |
| Develop a plugin | [Independent SDK](https://github.com/KMerdan/notepadnext-plugin-sdk), [native API 0.2](packages/plugin-sdk/docs/NativeAPI.zh-CN.md) |
| Understand current capability and limits | [Native SDK status](doc/plugin-system/NativeSDKStatus.zh-CN.md) |
| Understand architecture and future APIs | [Native SDK design](doc/plugin-system/NativeSDKDesign.zh-CN.md) |
| Find technical documentation | [Documentation index](doc/plugin-system/README.md) |

## Three repositories

| Repository | Owns |
| --- | --- |
| [notepad-next-plus-plus](https://github.com/KMerdan/notepad-next-plus-plus) | Native editor services, Qt UI, JSON-RPC host, plugin install/update/lifecycle and native tests |
| [notepadnext-plugin-sdk](https://github.com/KMerdan/notepadnext-plugin-sdk) | API types, metadata/wire contracts, templates, debugging/testing/service helpers and packaging tools |
| [Official Plugin Marketplace](https://github.com/KMerdan/notepadnext-markdown-preview) | Markdown/HTML plugin source, catalog and independent installable releases |

The SDK is a Git submodule pinned to a reviewed commit. The Marketplace consumes an exact SDK Release tarball through locked development dependencies. Its original repository URL is retained so existing Markdown installations keep their trusted source and data.

## Build and validate

The native SDK changes are developed on `codex/split-plugin-sdk` until the application PR is merged:

```sh
git clone --branch codex/split-plugin-sdk --recurse-submodules https://github.com/KMerdan/notepad-next-plus-plus.git
cd notepad-next-plus-plus
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --parallel
```

Core plugin support is enabled by default. Preview panels require Qt WebEngine/WebChannel and `-DNOTEPADNEXT_PLUGIN_WEBVIEWS=ON`. Bundle the pinned Node 24 binary using `-DNOTEPADNEXT_PLUGIN_NODE=/absolute/path/to/node`; [runtime preparation](doc/plugin-system/CrossPlatformCI.zh-CN.md) verifies the controlled archive. Set `-DNOTEPADNEXT_PLUGINS=OFF` for an editor-only build. Local macOS development requires no Developer ID or notarization.

For contract/integration tests, configure `-DNOTEPADNEXT_BUILD_PLUGIN_TESTS=ON` and the explicit Node test runtime. Run `ctest --test-dir build --output-on-failure`. See the [conformance workflow](.github/workflows/plugin-conformance.yml) for WebEngine, core-only and disabled-plugin configurations. Local macOS verification does not establish released support on every platform.

## Extend the editor

Native API 0.2 fixes the originating editor at command invocation, captures UTF-16 text/selections together, rejects stale document/selection writes, commits edit batches as one undo step and exposes eight existing editor operations through stable IDs. Native menus and plugins use the same `EditorOperations` implementation. Full HTML panels use `acquireNotepadNextApi()`; fragment and local Node service panels retain generic hot-update restoration.

Existing API 0.1 Markdown/HTML packages keep their callback contract and IDs. Register a repository through **Plugin Manager → Add Source…**; then explicitly install from **Discover**, enable in **Installed**, and open its command. Updates restore owned panels; uninstall is separate from removing a source.

The SDK's purpose is to enhance Notepad Next ++. Other editors' plugins may be studied and ported. A retained bounded VS Code adapter/importer is a historical experiment, isolated from the native SDK. File/search/language/bookmark/provider APIs in the larger design remain future work.

Trusted Node/Python plugins run in separate processes. Capabilities govern host RPC; they do not provide an operating-system sandbox. The Python prototype currently negotiates API 0.1 and needs an explicitly configured Python 3.12 interpreter.

## License and upstream

[GPL-3.0-or-later](LICENSE). Based on [dail8859/NotepadNext](https://github.com/dail8859/NotepadNext), copyright Justin Dailey and contributors. Fork modifications are maintained by KMerdan. Original notices and third-party licenses remain intact. Upstream build names such as the `NotepadNext` CMake target are retained; displayed product/executable are Notepad Next ++ / NotepadNextPlusPlus.
