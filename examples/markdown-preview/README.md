**Host integration fixture.** The independently maintained first-party plugin source and Releases live in the [official Marketplace](https://github.com/KMerdan/notepadnext-markdown-preview). This application example uses its own ID for repeatable host tests. Plugin authors should start from the [SDK templates](../../packages/plugin-sdk/README.md).

# Native Markdown preview example

This first-party TypeScript plugin uses the native `PluginContext` passed to `activate(context)`. It reads the editor buffer, including unsaved text, through host APIs. The production application's manager, menu, preview and exit path are exercised by Qt integration tests on macOS arm64. For a standalone author project, use the [SDK Markdown template and complete development walkthrough](../../packages/plugin-sdk/docs/MarkdownWalkthrough.zh-CN.md); it includes its own SDK, tests and source-map debugger.

Use Node 24, then build in this directory:

```sh
npm ci --ignore-scripts
npm run build
npm test
```

The SDK development dependency contains types; its import disappears from compiled JS. `package-lock.json` pins the renderer. After building, create a local release directory (the target must not already exist):

```sh
node ../../packages/plugin-sdk/pack.cjs . /tmp/markdown-preview-release
node ../../packages/plugin-sdk/archive.cjs /tmp/markdown-preview-release /tmp/notepadnext-plugin.zip
```

The packer copies `dist/`, optional `resources/`, production dependencies including nested versions, metadata, command icons and licenses. It excludes development dependencies and never evaluates plugin code or package hooks. The archive tool writes the bounded UTF-8 ZIP profile accepted by the native installer and prints its SHA-256. Both output paths must be new.

To create the ZIP and matching author checksums together, use a new output directory:

```sh
node ../../packages/plugin-sdk/release.cjs . /tmp/markdown-preview-assets v0.1.0
```

It creates `notepadnext-plugin.zip` and `notepadnext-plugin-checksums.json`. The release tag must match both metadata and package versions. A [standalone repository release workflow and checksum contract](../../doc/plugin-system/AuthorRelease.zh-CN.md) are available; CI execution on GitHub is still pending.

For local development, open **Plugins → Plugin Manager… → Add Source…**, select **Local metadata file**, and choose this example's `notepadnext-plugin.json`. Check and confirm registration. No GitHub repository, Developer ID or notarization is needed. Registration only adds the resource to **Discover**; it does not install or execute the plugin.

In **Discover**, select Markdown Preview, choose **Install Local Package…**, select the packed directory, and confirm installation. Installation leaves the plugin disabled. Enable it in **Installed**, then use **Open Markdown Preview** or **Plugins → Markdown Preview → Open Markdown Preview**. Type Markdown in the editor to preview unsaved changes. Disabling closes its panel and process; uninstalling returns it to Discover when its source still lists it. To install changed author code, increase the metadata version, rebuild and pack it, refresh its source in **Sources**, and use **Update…** with the new directory. **Reload** restarts the installed version; it does not copy author files into a package.

For GitHub distribution, set any explicit `repository` field to your real public repository, commit `notepadnext-plugin.json` at its root, and publish both generated assets under a stable Release tag matching its version, such as `v0.1.0`. A platform-specific asset may instead use `notepadnext-plugin-macos-arm64.zip` (the installer prefers its current platform/architecture); include its actual name, size and SHA-256 in the checksums. The installer uses GitHub's SHA-256 when supplied and otherwise requires the author's checksum file from that same Release. Do not upload the repository source ZIP as the plugin package. This example's repository URL is illustrative; this work has not published a plugin repository or Release.

Registering a repository or marketplace reads only metadata. **Install…** lists compatible assets and asks the user to select one before downloading it. The actual package must match the registered metadata. Refresh the source in **Sources** to register a newer version before choosing **Update…**. Updates preserve the enabled setting. A running plugin validates the candidate immediately. Disabled or inactive plugins receive static checks without execution; first activation validates the candidate and can restore the old version. Interrupted validation is rolled back when the index next loads, while versions explicitly awaiting first use remain installed.

The open command creates one panel and watches the active document. Refresh uses a 150ms debounce with an 800ms maximum wait. Invalidation cancels obsolete work; the host also checks document identity/version. Closing disposes the watch; reopening recreates it. Deactivation cancels timers and disposes the watch and panel; the runtime scope releases remaining command registrations.

The native panel uses the main window's palette and reading font. Inline code and code blocks use the active editor's default font and size, including changes made in Preferences. Palette and font changes update the existing page without reloading it; a palette change preserves its scroll position. The host owns these styles and escapes font names as CSS data. Plugins do not need another RPC capability to receive this presentation.

The renderer disables raw HTML and outputs XHTML for the native content policy. Relative PNG/JPEG/WebP images use `mapResource`. Missing, remote, escaping and untitled-document images become readable placeholders. Error states distinguish unsupported UTF-8 and snapshot size limits. Scroll synchronization and plugin-authored page scripts remain outside this example's current scope.
