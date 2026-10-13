Notepad Next ++ 0.15.2 adds explicit plugin categories, tab-owned previews and loss-protected file encodings.

Preview panels open only from a user command. Each preview plugin has one shared dock that follows matching active tabs and keeps each tab’s browser state. Nonmatching tabs hide it; closing the dock or the last matching document stops that preview. Hot updates restore only panels and documents that are still alive. HTML Preview now displays the document directly, without the demo controls or duplicate source editor.

The Encoding menu can reload bytes with a chosen character set or convert the next save format. UTF-8/UTF-16, Shift-JIS, GB18030 and common legacy code pages are supported. Saves reject unrepresentable characters before touching the file, and failed decoding leaves the current buffer intact. The macOS package now includes its declared application icon.

Register `https://github.com/KMerdan/notepadnext-plugin-marketplace` for Markdown Preview 0.1.8 and HTML Preview 0.1.4. Text Inspector and TODO Navigator are explicitly labeled SDK examples and are outside the default catalog. These previews require app 0.15.2 and SDK 0.3.2. Existing installs from the old marketplace origin require a one-time reinstall from the renamed repository; source identities are not silently migrated.

Choose the platform/architecture asset below. All packages include Node 24, the native API 0.3 plugin host and Qt WebEngine; no system Node/npm is needed for plugin use. `SHA256SUMS` covers all five packages. macOS requires version 14 or later; Linux packages target Ubuntu 24.04-compatible systems. macOS builds have ad-hoc signatures and are not notarized.

Installation and Homebrew instructions: https://github.com/KMerdan/notepad-next-plus-plus/blob/main/doc/Downloads.md

GPL-3.0-or-later. This independently maintained fork derives from dail8859/NotepadNext; original copyright notices and third-party licenses are retained.
