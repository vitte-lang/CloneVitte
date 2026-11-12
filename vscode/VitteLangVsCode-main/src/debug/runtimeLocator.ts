import { workspaceBinaryRoots, expandToolPath } from "../utils/project";
import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';

import * as cp from 'child_process';

interface VitteVersions {
  cli?: string;
  runtime?: string;
  lsp?: string;
  build?: string;
  fmt?: string;
  bench?: string;
}

/**
 * Utility for detecting the Vitte toolchain root and runtime binaries.
 * Used by DebugConfigurationProvider and factory.
 */
export interface VitteRuntimePaths {
  toolchainRoot?: string;
  cliPath?: string;
  runtimePath: string;
  lspPath?: string;
  buildPath?: string;
  fmtPath?: string;
  benchPath?: string;
  found: boolean;
  message?: string;
  versions?: VitteVersions;
}


// -----------------------------
// fs/helpers (cross-platform)
// -----------------------------
function isWindows(): boolean { return os.platform() === 'win32'; }

function exists(p: string | undefined): boolean {
  if (!p) return false;
  try { fs.accessSync(p, fs.constants.F_OK); return true; } catch { return false; }
}

function isExecutableSync(p: string | undefined): boolean {
  if (!p) return false;
  try {
    if (isWindows()) { fs.accessSync(p, fs.constants.F_OK); return true; }
    fs.accessSync(p, fs.constants.X_OK); return true;
  } catch { return false; }
}

function real(p: string | undefined): string | undefined {
  if (!p) return undefined;
  try {
    const rs = (fs.realpathSync as unknown) as { native?: (path: string) => string };
    if (typeof rs.native === 'function') {
      return rs.native(p);
    }
    return fs.realpathSync(p);
  } catch {
    return p;
  }
}

function firstNonEmpty(...values: (string | undefined)[]): string | undefined {
  for (const value of values) {
    if (value && value.length > 0) return value;
  }
  return undefined;
}

/** In-memory cache for lookups to avoid excessive fs access */
const WHICH_CACHE = new Map<string, string | undefined>();

/** Try resolve binary with PATH and common install roots. */
function which(bin: string): string | undefined {
  if (WHICH_CACHE.has(bin)) return WHICH_CACHE.get(bin)!;

  const envPath = process.env.PATH ?? '';
  const exts = isWindows() ? (process.env.PATHEXT?.split(';').filter(Boolean) ?? ['.exe', '.cmd', '.bat']) : [''];
  const parts = envPath.split(path.delimiter).filter(Boolean);

  for (const p of parts) {
    for (const ext of exts) {
      const full = path.join(p, bin + ext);
      if (isExecutableSync(full)) { const r = real(full) ?? full; WHICH_CACHE.set(bin, r); return r; }
    }
  }

  // Common extra roots: Homebrew, /usr/local, Nix, ~/.local/bin
  const candidates: string[] = [];
  const home = os.homedir();
  if (isWindows()) {
    candidates.push(path.join(process.env.ProgramFiles ?? 'C:/Program Files', 'Vitte', 'bin', `${bin}.exe`));
  } else {
    candidates.push('/opt/homebrew/bin/' + bin);
    candidates.push('/usr/local/bin/' + bin);
    candidates.push('/usr/bin/' + bin);
    candidates.push(path.join(home, '.local', 'bin', bin));
    // Nix store symlink path
    candidates.push('/run/current-system/sw/bin/' + bin);
    candidates.push('/nix/var/nix/profiles/default/bin/' + bin);
  }
  for (const c of candidates) {
    if (isExecutableSync(c)) { const r = real(c) ?? c; WHICH_CACHE.set(bin, r); return r; }
  }

  // As a last resort, try Homebrew prefix discovery (guarded)
  if (!isWindows()) {
    try {
      const out = cp.spawnSync('brew', ['--prefix'], { encoding: 'utf8', timeout: 350 });
      const prefix = (out.status === 0 && typeof out.stdout === 'string') ? out.stdout.trim() : '';
      if (prefix) {
        const full = path.join(prefix, 'bin', bin);
        if (isExecutableSync(full)) { const r = real(full) ?? full; WHICH_CACHE.set(bin, r); return r; }
      }
    } catch { /* ignore */ }
  }

  WHICH_CACHE.set(bin, undefined);
  return undefined;
}

// -----------------------------
// semver helpers
// -----------------------------
interface VSemVer { major: number; minor: number; patch: number; prerelease?: string; }

function parseSemver(input: string | undefined): VSemVer | undefined {
  if (!input) return undefined;
  const match = /v?(\d+)\.(\d+)\.(\d+)(?:[-+]([^\s]+))?/.exec(input.trim());
  if (!match) return undefined;
  const [, sMaj, sMin, sPatch, pre] = match;
  if (!sMaj || !sMin || !sPatch) return undefined;
  const ver: VSemVer = { major: Number.parseInt(sMaj, 10), minor: Number.parseInt(sMin, 10), patch: Number.parseInt(sPatch, 10) };
  if (pre !== undefined) ver.prerelease = pre;
  return ver;
}

function cmpSemver(a: VSemVer, b: VSemVer): number {
  if (a.major !== b.major) return a.major - b.major;
  if (a.minor !== b.minor) return a.minor - b.minor;
  if (a.patch !== b.patch) return a.patch - b.patch;
  // prereleases sort before stable
  if (a.prerelease && !b.prerelease) return -1;
  if (!a.prerelease && b.prerelease) return 1;
  return 0;
}

// -----------------------------
// discovery
// -----------------------------
function discoverWorkspaceBinRoots(): string[] {
  return workspaceBinaryRoots((vscode.workspace.workspaceFolders ?? []).map(folder => folder.uri.fsPath));
}

/** Detect toolchain root by checking settings, env vars, workspace bins, or bin location. */
export function detectVitteToolchainRoot(): string | undefined {
  const cfg = vscode.workspace.getConfiguration('vitte');
  const explicit = cfg.get<string>('toolchain.root') ?? cfg.get<string>('toolchainPath');
  if (explicit && isExecutableSync(path.join(explicit, isWindows() ? 'bin/vitte.exe' : 'bin/vitte'))) return explicit;
  if (explicit && isExecutableSync(path.join(explicit, isWindows() ? 'bin/vitte-runtime.exe' : 'bin/vitte-runtime'))) return explicit;

  const envRoot = firstNonEmpty(process.env.VITTE_TOOLCHAIN_ROOT, process.env.VITTE_ROOT, process.env.VITTE_HOME, process.env.VITTE_BIN_DIR);
  if (envRoot) {
    const candidate = path.join(envRoot, 'bin', isWindows() ? 'vitte.exe' : 'vitte');
    if (isExecutableSync(candidate)) return real(envRoot) ?? envRoot;
  }

  // workspace embedded bins (e.g., monorepo/tools)
  for (const r of discoverWorkspaceBinRoots()) {
    const candidate = path.join(r, isWindows() ? 'vitte.exe' : 'vitte');
    if (isExecutableSync(candidate)) return real(path.resolve(r, '..')) ?? path.resolve(r, '..');
  }

  const cli = which('vitte');
  if (cli) return path.resolve(path.dirname(cli), '..');

  const lsp = which('vitte-lsp');
  if (lsp) return path.resolve(path.dirname(lsp), '..');

  const runtime = which('vitte-runtime');
  if (runtime) return path.resolve(path.dirname(runtime), '..');

  return undefined;
}

function getVersion(bin: string | undefined): string | undefined {
  if (!bin) return undefined;
  try {
    const res = cp.spawnSync(bin, ['--version'], { encoding: 'utf8', timeout: 700 });
    const stdout = (res.stdout ?? '').trim();
    const stderr = (res.stderr ?? '').trim();
    const out = stdout.length > 0 ? stdout : stderr;
    const match = /\b(v?\d+\.\d+\.\d+(?:[-+].*)?)\b/.exec(out);
    if (match) return match[1];
    return out.length > 0 ? out : undefined;
  } catch { return undefined; }
}

// -----------------------------
// core location with memoization
// -----------------------------
let MEMO_RESULT: VitteRuntimePaths | undefined;

export function clearRuntimeLocatorCache(): void { MEMO_RESULT = undefined; }

function resolveFromSettingOrRoot(cfg: vscode.WorkspaceConfiguration, root: string | undefined, name: string, settingKey: string): string | undefined {
  const raw = cfg.get<string>(settingKey);
  const fromSetting = raw ? expandToolPath(raw, vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? process.cwd(), root) : undefined;
  if (fromSetting && isExecutableSync(fromSetting)) return real(fromSetting) ?? fromSetting;
  if (root) {
    const candidate = path.join(root, 'bin', name + (isWindows() ? '.exe' : ''));
    if (isExecutableSync(candidate)) return real(candidate) ?? candidate;
  }
  // workspace overrides
  for (const r of discoverWorkspaceBinRoots()) {
    const candidate = path.join(r, name + (isWindows() ? '.exe' : ''));
    if (isExecutableSync(candidate)) return real(candidate) ?? candidate;
  }
  return which(name);
}

/**
 * Locate runtime and companion binaries. Memoized per session.
 * Prefers workspace settings, then toolchain root, then workspace bins, then PATH.
 */
export function locateVitteRuntime(minRequired?: string): Promise<VitteRuntimePaths> {
  if (MEMO_RESULT) return Promise.resolve(MEMO_RESULT);

  const cfg = vscode.workspace.getConfiguration('vitte');
  const toolchainRoot = detectVitteToolchainRoot();

  const cliPath = resolveFromSettingOrRoot(cfg, toolchainRoot, 'vitte', 'compiler.path')
    ?? which('vitte');
  const runtimePath = cliPath ?? 'vitte';
  const lspPath   = resolveFromSettingOrRoot(cfg, toolchainRoot, 'vitte-lsp',   'lsp.path');
  const buildPath = resolveFromSettingOrRoot(cfg, toolchainRoot, 'vitte', 'build.path') ?? cliPath;
  const fmtPath   = resolveFromSettingOrRoot(cfg, toolchainRoot, 'vitte-fmt', 'fmt.path');
  const benchPath = resolveFromSettingOrRoot(cfg, toolchainRoot, 'vitte-bench', 'bench.path');

  const found = isExecutableSync(cliPath) || isExecutableSync(runtimePath);

  const result: VitteRuntimePaths = { runtimePath, found };
  if (cliPath) result.cliPath = cliPath;
  if (toolchainRoot) result.toolchainRoot = toolchainRoot;
  if (lspPath) result.lspPath = lspPath;
  if (buildPath) result.buildPath = buildPath;
  if (fmtPath) result.fmtPath = fmtPath;
  if (benchPath) result.benchPath = benchPath;

  // Versions
  const versions: VitteVersions = {};
  const vc = getVersion(found ? cliPath : undefined);
  if (vc) {
    versions.cli = vc;
  }
  const vr = getVersion(found ? runtimePath : undefined);
  if (vr) {
    versions.runtime = vr;
  }
  const vl = getVersion(lspPath);
  if (vl) {
    versions.lsp = vl;
  }
  const vb = getVersion(buildPath);
  if (vb) {
    versions.build = vb;
  }
  const vf = getVersion(fmtPath);
  if (vf) {
    versions.fmt = vf;
  }
  const vn = getVersion(benchPath);
  if (vn) {
    versions.bench = vn;
  }
  if (Object.keys(versions).length > 0) {
    result.versions = versions;
  }

  // Optional min version check
  if (minRequired && (versions.cli ?? versions.runtime)) {
    const have = parseSemver(versions.cli ?? versions.runtime);
    const need = parseSemver(minRequired);
    if (have && need && cmpSemver(have, need) < 0) {
      result.message = `vitte ${versions.cli ?? versions.runtime} is older than required ${minRequired}`;
    }
  }

  result.message ??= found
    ? `vitte detected at ${cliPath ?? runtimePath}`
    : 'vitte not found in PATH or toolchain root';

  MEMO_RESULT = result;
  return Promise.resolve(result);
}

/** Quick command for users: Detect and show runtime info in VSCode output */
export async function detectAndShowRuntimeInfo(output?: vscode.OutputChannel): Promise<void> {
  const result = await locateVitteRuntime();
  const out = output ?? vscode.window.createOutputChannel('Vitte');
  out.appendLine('[Vitte] Toolchain detection');
  if (result.toolchainRoot) out.appendLine(`Toolchain root: ${result.toolchainRoot}`);
  if (result.cliPath) out.appendLine(`CLI: ${result.cliPath}`);
  out.appendLine(`Runtime: ${result.runtimePath}`);
  if (result.lspPath) out.appendLine(`LSP: ${result.lspPath}`);
  if (result.buildPath) out.appendLine(`Build: ${result.buildPath}`);
  if (result.fmtPath) out.appendLine(`Fmt: ${result.fmtPath}`);
  if (result.benchPath) out.appendLine(`Bench: ${result.benchPath}`);
  if (result.versions) {
    const v = result.versions;
    out.appendLine('Versions:');
    if (v?.cli) out.appendLine(`  cli:     ${v.cli}`);
    if (v?.runtime) out.appendLine(`  runtime: ${v.runtime}`);
    if (v?.lsp) out.appendLine(`  lsp:     ${v.lsp}`);
    if (v?.build) out.appendLine(`  build:   ${v.build}`);
    if (v?.fmt) out.appendLine(`  fmt:     ${v.fmt}`);
    if (v?.bench) out.appendLine(`  bench:   ${v.bench}`);
  }
  out.appendLine(`Found: ${String(result.found)}`);
  if (result.message) out.appendLine(result.message);
  out.show(true);
}

/**
 * Register commands:
 *  - vitte.detectToolchain: show detected paths/versions
 *  - vitte.selectRuntime: let user pick a binary and persist to settings
 *  - vitte.clearRuntimeCache: invalidate memoized result
 */
export function registerRuntimeLocatorCommand(ctx: vscode.ExtensionContext): void {
  const detectCmd = vscode.commands.registerCommand('vitte.detectToolchain', async () => {
    await detectAndShowRuntimeInfo();
  });

  const clearCmd = vscode.commands.registerCommand('vitte.clearRuntimeCache', () => {
    clearRuntimeLocatorCache();
    vscode.window.showInformationMessage('Vitte: runtime cache cleared.');
  });

  const pickCmd = vscode.commands.registerCommand('vitte.selectRuntime', async () => {
    const opts: vscode.OpenDialogOptions = {
      canSelectMany: false,
      canSelectFiles: true,
      canSelectFolders: false,
      openLabel: 'Sélectionner vitte',
      title: 'Sélectionner le binaire vitte',
    };
    if (isWindows()) {
      opts.filters = { Exécutables: ['exe', 'cmd', 'bat'] };
    }
    const picked = await vscode.window.showOpenDialog(opts);
    const uri = picked?.[0];
    if (!uri) return;
    const fsPath = uri.fsPath;
    if (!isExecutableSync(fsPath)) {
      vscode.window.showErrorMessage('Le fichier sélectionné ne semble pas exécutable.');
      return;
    }
    await vscode.workspace.getConfiguration('vitte').update('compiler.path', fsPath, vscode.ConfigurationTarget.Workspace);
    clearRuntimeLocatorCache();
    vscode.window.showInformationMessage(`Vitte: CLI défini sur ${fsPath}`);
  });

  ctx.subscriptions.push(detectCmd, clearCmd, pickCmd);
}
