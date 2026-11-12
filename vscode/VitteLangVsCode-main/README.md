<p align="center">
  <img src="icons/vitte.png" alt="Vitte logo" width="112">
</p>

<h1 align="center">Vitte Studio for VS Code</h1>

<p align="center">
  Language support and a practical build, test, debug, and project workflow for Vitte.
</p>

<p align="center">
  <a href="https://marketplace.visualstudio.com/items?itemName=VitteStudio.vitte-studio"><img src="https://img.shields.io/visual-studio-marketplace/v/VitteStudio.vitte-studio?label=Marketplace&logo=visualstudiocode&color=007ACC" alt="Visual Studio Marketplace version"></a>
  <a href="https://marketplace.visualstudio.com/items?itemName=VitteStudio.vitte-studio"><img src="https://img.shields.io/visual-studio-marketplace/i/VitteStudio.vitte-studio?label=installs&color=2ea44f" alt="Marketplace installs"></a>
  <img src="https://img.shields.io/badge/VS%20Code-1.105%2B-007ACC?logo=visualstudiocode" alt="VS Code 1.105 or later">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT license">
</p>

<p align="center">
  <a href="https://marketplace.visualstudio.com/items?itemName=VitteStudio.vitte-studio">Install extension</a> ·
  <a href="https://vitte.netlify.app">Documentation</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="https://github.com/vitte-lang/VitteLangVsCode/issues">Report an issue</a>
</p>

---

Write Vitte with the language server, editor support, and project commands close at hand. The extension supports canonical `.vit` and `.vitl` files, plus `.vitte` for legacy projects.

| ✍️ **Edit** | 🧭 **Understand** | ⚙️ **Build** | 🧠 **Suggestions** |
| --- | --- | --- | --- |
| Syntax highlighting, completion, hover, formatting, and semantic tokens | Diagnostics, navigation, references, rename, and code actions | Build, run, test, clean, profiles, and incremental builds | Local-first inline suggestions with optional, privacy-gated AI |

```text
Open a .vit file  →  Detect the toolchain  →  Edit with live feedback
                                  ↓
                         Build · Run · Test
```

## Language support

- `.vit` — Vitte source
- `.vitl` — library and standard-library source
- `.vitte` — legacy compatibility
- Grammar-aligned highlighting, snippets, completion, signature help, hover, diagnostics, semantic tokens, and formatting
- Go to definition, find references, document/workspace symbols, and rename
- Native DAP launch for `.vit` sources, with external DAP stdio and TCP attach retained for advanced/native targets

### IntelliSense parity

Vitte follows the same editor-facing IntelliSense surface as the Microsoft
C/C++ extension where the LSP model applies: completion, signature help,
hover, diagnostics, symbols, navigation, references, rename, code actions,
formatting, semantic tokens, inlay hints, and call/type hierarchies are
exposed by the bundled language server and covered by the extension tests.

The native debug path compiles with DWARF symbols and `#line` source mappings,
then exposes the debuggee lifecycle through a persistent LLDB-backed DAP
adapter. Breakpoints, source-level stepping, multi-frame stacks, scoped locals,
aggregate/pointer expansion, watch expressions, conditional breakpoints,
logpoints, and `setVariable` are resolved from the native debug information;
there is no compiler probe channel in this path. External adapters remain
supported through `adapter` or `vitte.debug.program`.

The 2.3 release synchronizes editor support with the current parser grammar, including `return` / `give`, procedure contracts (`requires` / `ensures`), `pick` variants, `match` arms, and external procedures. Shared lexical scanning keeps completion, navigation, indexing, and semantic highlighting consistent inside nested comments and strings.

```vit
space hello

proc main() -> int {
    give 0;
}
```

## Get started

1. Install **Vitte Language Support** from the [Visual Studio Marketplace](https://marketplace.visualstudio.com/items?itemName=VitteStudio.vitte-studio), or install a `.vsix` with **Extensions: Install from VSIX...**.
2. Open a Vitte workspace and run **Vitte: Detect Toolchain** from the Command Palette (`Ctrl/Cmd+Shift+P`).
3. Run **Vitte: Quick** and choose **Run full setup**, or start with **Vitte: Build**.

Debugging uses the bundled native DAP backend by default. It compiles a `.vit`
source with `--debug-info --line-directives`, launches the resulting native
program, and delegates native inspection to LLDB/DWARF. The bundled native
adapter requires `lldb` and `expect` on the executable search path. Configure
`vitte.debug.program` or `adapter` to select an external DAP adapter when a
different target toolchain is required. To connect to an already running
adapter, use **Vitte: Debug: Attach** or set `request` to `attach` with a
`host` and `port`; attach always uses TCP.

The extension uses its bundled TypeScript language server by default. Toolchain, compiler, language-server, and formatter paths can be set explicitly when auto-detection is not suitable.

## A compact command center

| Command | Use it to… |
| --- | --- |
| **Vitte: Quick** | Open the curated action menu and setup flow |
| **Vitte: Build / Run / Test** | Compile, execute, or validate the workspace |
| **Vitte: Test Current File** | Focus tests on the active source file |
| **Vitte: Open Playground** | Try a small Vitte example in an isolated editor |
| **Vitte: Project Assistant / Doctor** | Create project files or diagnose setup problems |
| **Vitte: Show Server Log / Show Server Metrics** | Inspect language-server activity and performance |
| **Vitte: Restart Language Server** | Recover completion or diagnostics after a stalled session |
| **Vitte: Explain Offline / Copy Offline Report** | Understand connectivity state and collect a support report |

## Suggestions, local first

The local inline engine is enabled by default. It indexes workspace Vitte files and ranks context-aware suggestions without sending code to an external service.

The optional AI pipeline is off by default. Its privacy defaults are deliberately conservative:

| Setting | Default |
| --- | --- |
| Local-only suggestions | **On** |
| Cloud consent (`cloudOptIn`) | **Off** |
| AI pipeline and dedicated backend | **Off** |
| Trusted workspace required | **On** |
| Secret redaction | **On** |
| External training | **Off** |
| Requested data retention | **0 days** |

Use **Vitte: Suggestions Cloud Opt-In** only when you intend to enable a configured backend. **Vitte: Suggestions Cloud Opt-Out (Local-Only)** restores local-only operation. The extension also provides local-engine stats, a profiler, and diagnostics export.

## Configure only what you need

Open **Preferences: Open Settings (UI)** and search for `vitte`.

| Setting | Purpose |
| --- | --- |
| `vitte.toolchain.root` | Toolchain installation root |
| `vitte.compiler.path` | Compiler binary used for live diagnostics |
| `vitte.lsp.path` | Optional external language-server binary |
| `vitte.features.signatureHelp` | Enable or disable procedure signature help |
| `vitte.debug.nativeBackend` | Use the bundled native process DAP backend when no external adapter is configured |
| `vitte.debug.program` | Optional external executable that implements DAP |
| `vitte.debug.adapterArgs` | Startup arguments for the DAP executable, separate from debuggee arguments |
| `vitte.debug.adapterTransport` | Default adapter transport for launch: `stdio` or an existing TCP server |
| `vitte.fmt.path` | Optional external formatter |
| `vitte.build.profile` | `dev`, `test`, `release`, or `bench` |
| `vitte.build.incremental` | Enable incremental builds when supported |
| `vitte.suggestions.localEngine.*` | Tune local indexing and suggestion behavior |
| `vitte.suggestions.aiPipeline.*` | Configure optional backend and privacy controls |

## When something feels off

1. Run **Vitte: Detect Toolchain** to check binary discovery.
2. Run **Vitte: Restart Language Server** if editor features have stopped refreshing.
3. Open **Vitte: Show Server Log** for technical details.
4. If the extension is offline, use **Vitte: Explain Offline**, then **Vitte: Copy Offline Report** when sharing diagnostics.

## Project

- Extension: `VitteStudio.vitte-studio` · version `2.3.0`
- Requires VS Code `1.105` or later
- License: [MIT](LICENSE)
- Release notes: [CHANGELOG.md](CHANGELOG.md)
- Repository: [vitte-lang/VitteLangVsCode](https://github.com/vitte-lang/VitteLangVsCode)

## Development and validation

Run these commands from `vscode/VitteLangVsCode-main` in the compiler repository,
or from the extension root in a standalone checkout:

```sh
npm ci
npm run test:unit
npm run lint
npm test
npm run vsix
```

`test:unit` builds the client and server, then checks lexical boundaries, grammar
vocabulary, TextMate tokenization, completion, indexing, signatures, semantic
tokens, and a real stdio LSP session without starting a graphical editor.
`npm test` additionally runs the VS Code integration suite, which needs a display
(on Linux CI, use `xvfb-run -a npm test`). Import paths are regenerated from this
repository's modules during the build; standalone checkouts retain the checked-in
catalog. Server output copying uses Node.js and works on Windows, macOS, and Linux.
