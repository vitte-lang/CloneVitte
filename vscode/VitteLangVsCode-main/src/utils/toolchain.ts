import { workspaceBinaryRoots, expandToolPath } from "./project";
import * as vscode from 'vscode';
import * as path from 'path';
import * as os from 'os';
import * as cp from 'child_process';
import { exists } from './fs';

/**
 * Vitte toolchain resolver
 * - Detects toolchain root from settings/env/which
 * - Resolves the current unified `vitte` CLI plus optional companion tools
 * - Reads versions lazily ("--version")
 * - Caches results in globalState with a short TTL to avoid costy lookups
 */

export type BinKind = 'cli' | 'runtime' | 'lsp' | 'build' | 'fmt' | 'bench';

export interface ToolchainInfo {
  /** Toolchain root directory, if known (folder that contains `bin/`). */
  root?: string;
  /** Map of resolved absolute binary paths. Missing keys are `undefined`. */
  bins: Partial<Record<BinKind, string>>;
  /** Parsed versions per binary (e.g., `vitte --version`). */
  versions: Partial<Record<BinKind, string>>;
  /** True if at least the main runtime was found. */
  ok: boolean;
  /** Human-readable notes and warnings. */
  messages: string[];
}

interface CacheEntry {
  info: ToolchainInfo;
  ts: number; // epoch ms
}

const CACHE_KEY = 'vitte.toolchain.cache';
const CACHE_TTL_MS = 30_000; // 30s — safe for repeated commands

/** Resolve the configured toolchain root from settings/env, or undefined. */
export function configuredToolchainRoot(): string | undefined {
  const cfg = vscode.workspace.getConfiguration('vitte');
  const s1 = cfg.get<string>('toolchain.root');
  const s2 = cfg.get<string>('toolchainPath'); // legacy
  const env = process.env.VITTE_TOOLCHAIN_ROOT;
  return firstNonEmpty(s1, s2, env);
}

/** Try to resolve a binary path by checking setting, toolchain root, then PATH. */
export async function resolveBinary(
  name: string,
  opts?: { settingKeys?: string[] | undefined; root?: string | undefined }
): Promise<string | undefined> {
  const cfg = vscode.workspace.getConfiguration('vitte');
  // 1) explicit settings
  const keys = opts?.settingKeys ?? [];
  for (const k of keys) {
    const raw = cfg.get<string>(k)?.trim();
    if (raw) {
      const p = expandToolPath(raw, vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? process.cwd(), opts?.root);
      if (await exists(p)) return p;
    }
  }
  // 2) toolchain root/bin
  const root = opts?.root ?? configuredToolchainRoot();
  if (root) {
    const candidate = path.join(root, 'bin', nameWithExt(name));
    if (await exists(candidate)) return candidate;
  }
  for (const binRoot of workspaceBinaryRoots((vscode.workspace.workspaceFolders ?? []).map(folder => folder.uri.fsPath))) {
    const candidate = path.join(binRoot, nameWithExt(name));
    if (await exists(candidate)) return candidate;
  }
  // 3) PATH lookup
  return await which(name);
}

/** Main resolution entry point with short-lived cache. */
export async function getToolchain(ctx?: vscode.ExtensionContext, forceRefresh = false): Promise<ToolchainInfo> {
  const state = ctx?.globalState;
  if (!forceRefresh && state) {
    const cache = state.get<CacheEntry>(CACHE_KEY);
    if (cache && Date.now() - cache.ts < CACHE_TTL_MS) return cache.info;
  }

  const messages: string[] = [];
  const root = configuredToolchainRoot();
  if (!root) messages.push('Aucun "vitte.toolchain.root" configuré — recherche via PATH…');

  const rootOpt = root !== undefined ? { root } : undefined;
  const cli = await resolveBinary('vitte', { settingKeys: ['compiler.path'], ...rootOpt });
  const runtime = cli;
  const lsp = await resolveBinary('vitte-lsp', { settingKeys: ['lsp.path'], ...rootOpt });
  const build = await resolveBinary('vitte', { settingKeys: ['build.path', 'compiler.path'], ...rootOpt }) ?? cli;
  const fmt = await resolveBinary('vitte-fmt', { settingKeys: ['fmt.path'], ...rootOpt });
  const bench = await resolveBinary('vitte-bench', { settingKeys: ['bench.path'], ...rootOpt });

  const bins: ToolchainInfo['bins'] = {};
  if (cli !== undefined) bins.cli = cli;
  if (runtime !== undefined) bins.runtime = runtime;
  if (lsp !== undefined) bins.lsp = lsp;
  if (build !== undefined) bins.build = build;
  if (fmt !== undefined) bins.fmt = fmt;
  if (bench !== undefined) bins.bench = bench;

  const ok = !!cli || !!runtime;
  if (!ok) messages.push('vitte introuvable (paramètre vitte.compiler.path, vitte.toolchain.root ou PATH).');

  const info: ToolchainInfo = { bins, versions: {}, ok, messages };
  if (root !== undefined) info.root = root;

  // Optionally read versions (best-effort, don't throw)
  await Promise.all(
    (Object.entries(bins) as [BinKind, string][]).map(async ([k, p]) => {
      const ver = await readVersion(p);
      if (ver) info.versions[k] = ver;
    })
  );

  if (state) await state.update(CACHE_KEY, { info, ts: Date.now() } as CacheEntry);
  return info;
}

/** Force refresh of the toolchain cache. */
export async function refreshToolchain(ctx?: vscode.ExtensionContext): Promise<ToolchainInfo> {
  if (ctx) await ctx.globalState.update(CACHE_KEY, undefined);
  return getToolchain(ctx, true);
}

// ------------------------- internals -------------------------

function nameWithExt(name: string): string {
  // Defensive: ensure a string even under odd narrowings
  if (!name) return '';
  if (process.platform === 'win32') {
    const pathext = (process.env.PATHEXT ?? '.EXE;.CMD;.BAT').toLowerCase().split(';');
    for (const ext of pathext) if (name.toLowerCase().endsWith(ext.toLowerCase())) return name; // already has an ext
    return name + '.exe';
  }
  return name;
}

/** PATH lookup that respects PATHEXT on Windows. */
export async function which(bin: string | undefined): Promise<string | undefined> {
  if (!bin) return undefined;
  const b: string = bin; // narrow to plain string for downstream calls
  const parts = (process.env.PATH ?? '').split(path.delimiter).filter(Boolean);
  const exts = process.platform === 'win32' ? (process.env.PATHEXT ?? '.EXE;.CMD;.BAT').split(';') : [''];
  for (const dir of parts) {
    for (const ext of exts) {
      const candidate = path.join(dir, b + ext);
      if (await exists(candidate)) return candidate;
    }
  }
  return undefined;
}

async function readVersion(bin: string): Promise<string | undefined> {
  try {
    const out = await execCapture([bin, '--version']);
    const match = /\b(\d+\.\d+\.\d+(?:[-+][\w.]+)?)\b/.exec(String(out));
    return match ? match[1] : String(out).trim().split(/\r?\n/)[0];
  } catch { return undefined; }
}

function firstNonEmpty(...values: (string | undefined | null)[]): string | undefined {
  for (const value of values) {
    if (typeof value === 'string') {
      const trimmed = value.trim();
      if (trimmed.length > 0) return trimmed;
    }
  }
  return undefined;
}

/** Execute a command and capture stdout (utf-8). */
async function execCapture(cmd: string[] | string, opts?: { cwd?: string; env?: NodeJS.ProcessEnv; timeoutMs?: number }): Promise<string> {
  return new Promise((resolve, reject) => {
    const timeout = Math.max(1000, Math.min(10_000, opts?.timeoutMs ?? 3000));

    const fileOrArray = Array.isArray(cmd) ? cmd[0] : cmd;
    const args: string[] = Array.isArray(cmd) ? cmd.slice(1) : [];

    // ⛑️ Narrow strict: forbid undefined/empty command reaching spawn
    if (typeof fileOrArray !== 'string' || fileOrArray.length === 0) {
      reject(new Error('invalid command'));
      return;
    }
    const file: string = fileOrArray;

    const options: cp.SpawnOptions = {
      cwd: opts?.cwd,
      env: opts?.env ?? process.env,
      shell: false,
    };

    const child = cp.spawn(file, args, options); // type: cp.ChildProcess

    let out = '';
    let err = '';

    const to = setTimeout(() => {
      try { child.kill(); } catch { /* ignore */ }
      reject(new Error('timeout'));
    }, timeout);

    child.stdout?.on('data', (b: Buffer) => { out += b.toString(); });
    child.stderr?.on('data', (b: Buffer) => { err += b.toString(); });
    child.on('error', (e) => { clearTimeout(to); reject(e); });
    child.on('close', (code) => {
      clearTimeout(to);
      if (code === 0) resolve(out);
      else if (out) resolve(out); // some tools print version even on non-zero exit
      else reject(new Error(err || `exit ${code}`));
    });
  });
}

/** Convenience: return a friendly snapshot string (for status bars, etc.). */
export function formatToolchainStatus(info: ToolchainInfo): string {
  const v = info.versions.cli ?? info.versions.runtime ?? info.versions.lsp ?? info.versions.build;
  const ver = v ? `v${v}` : 'unknown';
  const ok = info.ok ? '✓' : '✗';
  return `Vitte ${ok} · ${ver}`;
}

/** Returns recommended default env for child processes (PATH patched with toolchain bin). */
export function childEnvWithToolchain(info: ToolchainInfo, base: NodeJS.ProcessEnv = process.env): NodeJS.ProcessEnv {
  const env = { ...base };
  let binDir: string | undefined;
  const r = info.root;
  if (typeof r === 'string' && r.length > 0) {
    binDir = path.join(r, 'bin');
  }
  if (binDir) env.PATH = binDir + path.delimiter + (base.PATH ?? '');
  env.HOME ??= os.homedir();
  return env;
}

