Notepad Next ++ 0.15.1 adds native SDK API 0.3 and plugin-enabled binary packages for macOS Apple Silicon/Intel, Windows x86-64 and Linux x86-64.

The native SDK exposes bounded text reads, document/selection events, file lifecycle, cancellable literal search, language/EOL/bookmarks, editor commands and session-owned highlights. The plugin manager supports registration, install/update, enable/disable, uninstall and automatic restoration of open panels after hot updates.

Register `https://github.com/KMerdan/notepadnext-markdown-preview` to install Markdown Preview, HTML Preview, Text Inspector and TODO Navigator. This version uses one native API profile, 0.3; removed legacy profiles, Python prototype and VS Code adapters are not bundled. Rebuild older custom plugins with SDK 0.3.

Choose the platform/architecture asset below. All packages include Node 24 and Qt WebEngine; no system Node/npm is needed for plugin use. `SHA256SUMS` covers all five packages. macOS requires version 14 or later; Linux packages target Ubuntu 24.04-compatible systems. macOS builds have ad-hoc signatures and are not notarized.

Installation and Homebrew instructions: https://github.com/KMerdan/notepad-next-plus-plus/blob/main/doc/Downloads.md

GPL-3.0-or-later. This independently maintained fork derives from dail8859/NotepadNext; original copyright notices and third-party licenses are retained.
