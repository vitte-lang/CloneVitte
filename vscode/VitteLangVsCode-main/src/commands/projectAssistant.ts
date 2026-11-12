import { workspaceConfiguration, projectSource, PROJECT_CONFIG } from "../utils/project";
import { locateVitteRuntime } from "../debug/runtimeLocator";
import * as path from "node:path";
import * as fs from "node:fs/promises";
import { spawn } from "node:child_process";
import * as vscode from "vscode";
import type { LanguageClient } from "vscode-languageclient/node";

function rootDir(): string | undefined {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function workspaceRoots(): string[] {
  return (vscode.workspace.workspaceFolders ?? []).map((f) => f.uri.fsPath);
}

function isSafeChildPath(base: string, target: string): boolean {
  const rel = path.relative(base, target);
  return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
}

function normalizeProjectName(input: string): string | undefined {
  const value = input.trim();
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(value)) return undefined;
  return value;
}

function normalizeModulePath(input: string): string | undefined {
  const rel = input.trim().replace(/\\/g, "/").replace(/^\/+|\/+$/g, "");
  if (!rel) return undefined;
  const parts = rel.split("/");
  if (parts.some((part) => part === "." || part === ".." || part.length === 0)) return undefined;
  if (!parts.every((part) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(part))) return undefined;
  return rel;
}

function normalizePackageName(input: string): string | undefined {
  const value = input.trim();
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(value)) return undefined;
  return value;
}

async function promptOpenProjectFolder(dir: string): Promise<void> {
  const uri = vscode.Uri.file(dir);
  const pick = await vscode.window.showInformationMessage(
    `Project created: ${dir}`,
    "Open Here",
    "Open New Window",
    "Keep Current Window",
  );
  if (pick === "Open Here") {
    await vscode.commands.executeCommand("vscode.openFolder", uri, false);
  } else if (pick === "Open New Window") {
    await vscode.commands.executeCommand("vscode.openFolder", uri, true);
  }
}

async function ensureDir(p: string): Promise<void> {
  await fs.mkdir(p, { recursive: true });
}

async function writeIfMissing(p: string, content: string): Promise<void> {
  try {
    await fs.access(p);
  } catch {
    await ensureDir(path.dirname(p));
    await fs.writeFile(p, content, "utf8");
  }
}

async function existsFile(p: string): Promise<boolean> {
  try {
    await fs.access(p);
    return true;
  } catch {
    return false;
  }
}

async function writeWithOverwriteConfirm(p: string, content: string): Promise<boolean> {
  if (await existsFile(p)) {
    const pick = await vscode.window.showWarningMessage(
      `File already exists: ${path.basename(p)}. Overwrite?`,
      { modal: true },
      "Overwrite",
      "Cancel",
    );
    if (pick !== "Overwrite") return false;
  } else {
    await ensureDir(path.dirname(p));
  }
  await fs.writeFile(p, content, "utf8");
  return true;
}

async function generateWorkspaceVscodeFiles(base: string): Promise<void> {
  const tasksPath = path.join(base, ".vscode", "tasks.json");
  const launchPath = path.join(base, ".vscode", "launch.json");
  const runtime = await locateVitteRuntime();
  const { tasks, launch } = workspaceConfiguration(base, runtime.cliPath ?? "vitte");

  await writeIfMissing(tasksPath, `${JSON.stringify(tasks, null, 2)}\n`);
  await writeIfMissing(launchPath, `${JSON.stringify(launch, null, 2)}\n`);
}

async function createNewProject(): Promise<void> {
  const ws = rootDir();
  if (!ws) {
    void vscode.window.showErrorMessage("Open a workspace folder first.");
    return;
  }
  const input = await vscode.window.showInputBox({ prompt: "New project name (letters, digits, underscore)", value: "my_vitte_app" });
  if (!input) return;
  const name = normalizeProjectName(input);
  if (!name) {
    void vscode.window.showErrorMessage("Invalid project name. Use letters, digits, and underscore only.");
    return;
  }
  const dir = path.join(ws, name);
  if (!isSafeChildPath(ws, dir)) {
    void vscode.window.showErrorMessage("Invalid project path.");
    return;
  }
  await ensureDir(path.join(dir, "src"));
  await writeIfMissing(path.join(dir, "src", "main.vit"), projectSource(name));
  await writeIfMissing(path.join(dir, "vitte.toml"), PROJECT_CONFIG);
  await generateWorkspaceVscodeFiles(dir);
  await promptOpenProjectFolder(dir);
}

async function createNewModule(): Promise<void> {
  const ws = rootDir();
  if (!ws) return;
  const mod = await vscode.window.showInputBox({ prompt: "Module path (example: app/network/http)" });
  if (!mod) return;
  const rel = normalizeModulePath(mod);
  if (!rel) {
    void vscode.window.showErrorMessage("Invalid module path. Use slash-separated identifiers (letters, digits, underscore).");
    return;
  }
  const file = path.join(ws, "src", `${rel}.vit`);
  if (!isSafeChildPath(ws, file)) {
    void vscode.window.showErrorMessage("Invalid module path.");
    return;
  }
  const wrote = await writeWithOverwriteConfirm(file, `space ${rel};\n\npub proc run() -> i32 {\n  return 0;\n}\n`);
  if (!wrote) return;
  await generateWorkspaceVscodeFiles(ws);
  const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
  await vscode.window.showTextDocument(doc, { preview: false });
}

async function createNewPackage(): Promise<void> {
  const ws = rootDir();
  if (!ws) return;
  const input = await vscode.window.showInputBox({ prompt: "Package name", value: "core_utils" });
  if (!input) return;
  const name = normalizePackageName(input);
  if (!name) {
    void vscode.window.showErrorMessage("Invalid package name. Use letters, digits, and underscore only.");
    return;
  }
  const dir = path.join(ws, "modules", name);
  if (!isSafeChildPath(ws, dir)) {
    void vscode.window.showErrorMessage("Invalid package path.");
    return;
  }
  await ensureDir(path.join(dir, "src"));
  const modFile = path.join(dir, "src", "lib.vit");
  const pkgPath = name;
  const wrote = await writeWithOverwriteConfirm(
    modFile,
    `space ${pkgPath};\n\npub proc ready() -> bool {\n  return true;\n}\n`,
  );
  if (!wrote) return;
  await generateWorkspaceVscodeFiles(ws);
  const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(modFile));
  await vscode.window.showTextDocument(doc, { preview: false });
}

type DoctorTool = "check" | "lex" | "parse";

interface ToolRunResult {
  tool: DoctorTool;
  cmd: string;
  args: string[];
  code: number;
  stdout: string;
  stderr: string;
  durationMs: number;
}

const doctorCollection = vscode.languages.createDiagnosticCollection("vitte-doctor");


function asStringPrimitive(value: unknown): string | undefined {
  if (typeof value === "string") return value;
  if (typeof value === "number" || typeof value === "boolean" || typeof value === "bigint") return String(value);
  return undefined;
}

function firstText(...values: unknown[]): string | undefined {
  for (const value of values) {
    const text = asStringPrimitive(value);
    if (text && text.length > 0) return text;
  }
  return undefined;
}

function sanitizeCode(code: string): string {
  return code.replace(/[^A-Za-z0-9_.:-]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 120) || "UNKNOWN";
}

function severityFromAny(value: unknown): vscode.DiagnosticSeverity {
  const s = (asStringPrimitive(value) ?? "").toLowerCase();
  if (s.includes("error") || s === "err") return vscode.DiagnosticSeverity.Error;
  if (s.includes("warn")) return vscode.DiagnosticSeverity.Warning;
  if (s.includes("info")) return vscode.DiagnosticSeverity.Information;
  return vscode.DiagnosticSeverity.Hint;
}

function parseLineInt(value: unknown, fallback: number): number {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(0, Math.floor(n));
}

function extractJsonValues(text: string): unknown[] {
  const out: unknown[] = [];
  const trimmed = text.trim();
  if (!trimmed) return out;
  try {
    out.push(JSON.parse(trimmed));
  } catch {
    // line-based fallback
  }
  for (const line of text.split(/\r?\n/)) {
    const t = line.trim();
    if (!t || (!t.startsWith("{") && !t.startsWith("["))) continue;
    try {
      out.push(JSON.parse(t));
    } catch {
      // ignore invalid JSON line
    }
  }
  return out;
}

function normalizeRecords(value: unknown): Record<string, unknown>[] {
  if (Array.isArray(value)) return value.filter((v): v is Record<string, unknown> => !!v && typeof v === "object");
  if (value && typeof value === "object") {
    const obj = value as Record<string, unknown>;
    if (Array.isArray(obj.items)) return normalizeRecords(obj.items);
    if (Array.isArray(obj.diagnostics)) return normalizeRecords(obj.diagnostics);
    return [obj];
  }
  return [];
}

function fileUriFromRecord(ws: string, rec: Record<string, unknown>): vscode.Uri {
  const locationFile = typeof rec.position === "object" && rec.position
    ? (rec.position as Record<string, unknown>).source
    : typeof rec.location === "object" && rec.location
    ? (rec.location as Record<string, unknown>).file
    : undefined;
  const pathLike = firstText(rec.uri, rec.file, rec.path, locationFile) ?? "";
  if (pathLike.startsWith("file://")) return vscode.Uri.parse(pathLike);
  if (pathLike) {
    const fsPath = path.isAbsolute(pathLike) ? pathLike : path.join(ws, pathLike);
    return vscode.Uri.file(fsPath);
  }
  return vscode.Uri.file(path.join(ws, "vitte.toml"));
}

function diagnosticsFromToolResult(ws: string, res: ToolRunResult): Map<string, vscode.Diagnostic[]> {
  const map = new Map<string, vscode.Diagnostic[]>();
  const payloads = extractJsonValues(res.stdout);
  const records = payloads.flatMap(normalizeRecords);
  if (records.length === 0) {
    if (res.code !== 0 || res.stderr.trim()) {
      const uri = vscode.Uri.file(path.join(ws, "vitte.toml"));
      const d = new vscode.Diagnostic(
        new vscode.Range(0, 0, 0, 1),
        `${res.tool} failed (${res.code}): ${res.stderr.trim() || "no structured output"}`,
        vscode.DiagnosticSeverity.Error
      );
      d.code = `DOCTOR:${res.tool.toUpperCase()}`;
      d.source = "vitte";
      map.set(uri.toString(), [d]);
    }
    return map;
  }

  for (const rec of records) {
    const uri = fileUriFromRecord(ws, rec);
    const line = parseLineInt(
      rec.line ?? (typeof rec.position === "object" && rec.position ? (rec.position as Record<string, unknown>).start_line : undefined) ?? (typeof rec.location === "object" && rec.location ? (rec.location as Record<string, unknown>).line : 1),
      1
    ) - 1;
    const col = parseLineInt(
      rec.column ?? rec.col ?? (typeof rec.position === "object" && rec.position ? (rec.position as Record<string, unknown>).start_column : undefined) ?? (typeof rec.location === "object" && rec.location ? (rec.location as Record<string, unknown>).column : 1),
      1
    ) - 1;
    const message = firstText(rec.message, rec.msg, rec.error, rec.reason) ?? `${res.tool} issue`;
    const code = sanitizeCode(firstText(rec.code) ?? res.tool.toUpperCase());
    const severity = severityFromAny(rec.severity ?? rec.level ?? (res.code === 0 ? "warning" : "error"));
    const diag = new vscode.Diagnostic(
      new vscode.Range(Math.max(0, line), Math.max(0, col), Math.max(0, line), Math.max(1, col + 1)),
      message,
      severity
    );
    if (code.startsWith("DOCTOR:")) {
      diag.code = code;
    } else if (code.startsWith("DOCTOR_")) {
      diag.code = `DOCTOR:${code.slice("DOCTOR_".length)}`;
    } else {
      diag.code = `DOCTOR:${res.tool.toUpperCase()}_${code}`;
    }
    diag.source = "vitte";
    const arr = map.get(uri.toString()) ?? [];
    arr.push(diag);
    map.set(uri.toString(), arr);
  }
  return map;
}

async function runTool(ws: string, tool: DoctorTool): Promise<ToolRunResult> {
  const runtime = await locateVitteRuntime();
  const current = vscode.window.activeTextEditor?.document.uri;
  const candidates = [
    ...(current?.scheme === "file" && /\.(vit|vitl|vitte)$/i.test(current.fsPath) ? [current.fsPath] : []),
    path.join(ws, "src/main.vit"), path.join(ws, "main.vit"), path.join(ws, "examples/hello.vit"),
  ];
  let input: string | undefined;
  for (const candidate of candidates) if (await existsFile(candidate)) { input = candidate; break; }
  const cmd = runtime.cliPath ?? "vitte";
  const args = [tool, "--diagnostic=json", "--no-color", ...(input ? [input] : [])];
  if (!input) return { tool, cmd, args, code: 1, stdout: "", stderr: "Open a Vitte source file or create src/main.vit before running Doctor.", durationMs: 0 };
  const t0 = Date.now();
  return await new Promise<ToolRunResult>((resolve) => {
    const child = spawn(cmd, args, { cwd: ws, shell: false });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (d) => { stdout += String(d); });
    child.stderr.on("data", (d) => { stderr += String(d); });
    child.on("error", (err) => {
      resolve({ tool, cmd, args, code: 127, stdout, stderr: `${stderr}\n${String(err)}`, durationMs: Date.now() - t0 });
    });
    child.on("close", (code) => {
      resolve({ tool, cmd, args, code: code ?? 1, stdout, stderr, durationMs: Date.now() - t0 });
    });
  });
}

async function runDoctor(output: vscode.OutputChannel, selected?: DoctorTool): Promise<void> {
  const roots = workspaceRoots();
  if (roots.length === 0) return;
  const tools: DoctorTool[] = selected ? [selected] : ["check"];
  const finalMap = new Map<string, vscode.Diagnostic[]>();
  output.appendLine(`[doctor] run ${tools.join(", ")} on ${roots.length} root(s)`);

  for (const ws of roots) {
    for (const tool of tools) {
      const res = await runTool(ws, tool);
      const relRoot = vscode.workspace.asRelativePath(vscode.Uri.file(ws), false) || ws;
      output.appendLine(`[doctor:${tool}] root=${relRoot} cmd=${res.cmd} ${res.args.join(" ")} code=${res.code} ${res.durationMs}ms`);
      if (res.stderr.trim()) output.appendLine(`[doctor:${tool}:stderr] ${res.stderr.trim()}`);
      const dm = diagnosticsFromToolResult(ws, res);
      for (const [key, list] of dm) {
        const prev = finalMap.get(key) ?? [];
        finalMap.set(key, [...prev, ...list]);
      }
    }
  }

  const entries: [vscode.Uri, vscode.Diagnostic[]][] = [];
  for (const [uriText, diags] of finalMap) {
    entries.push([vscode.Uri.parse(uriText), diags]);
  }
  if (!selected) doctorCollection.clear();
  if (entries.length > 0) doctorCollection.set(entries);
  const total = entries.reduce((acc, [, d]) => acc + d.length, 0);
  void vscode.window.showInformationMessage(`Vitte Doctor: ${total} issue(s) published to Problems.`);
}

async function runExtensionBench(
  clientGetter: () => LanguageClient | undefined,
  output: vscode.OutputChannel,
  activationMs?: number,
): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  const mem = process.memoryUsage();
  const sample: Record<string, unknown> = {
    ts: new Date().toISOString(),
    activationMs: activationMs ?? null,
    rssMB: Number((mem.rss / (1024 * 1024)).toFixed(2)),
    heapUsedMB: Number((mem.heapUsed / (1024 * 1024)).toFixed(2)),
  };

  const client = clientGetter();
  if (client) {
    const p0 = Date.now();
    try {
      await client.sendRequest("vitte/ping");
      sample.pingMs = Date.now() - p0;
    } catch (err) {
      sample.pingError = String(err);
    }
  }

  if (editor) {
    const pos = editor.selection.active;
    const latencies: number[] = [];
    for (let i = 0; i < 5; i++) {
      const t = Date.now();
      try {
        await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", editor.document.uri, pos, ".");
        latencies.push(Date.now() - t);
      } catch {
        // ignore
      }
    }
    if (latencies.length > 0) {
      sample.completionAvgMs = Number((latencies.reduce((a, b) => a + b, 0) / latencies.length).toFixed(2));
      sample.completionMaxMs = Math.max(...latencies);
    }
  }

  output.appendLine(`[bench-ci] ${JSON.stringify(sample)}`);
  const ws = rootDir();
  if (!ws) return;
  const outFile = path.join(ws, ".vitte-cache", "diagnostics", "extension-bench.json");
  await ensureDir(path.dirname(outFile));
  await fs.writeFile(outFile, `${JSON.stringify(sample, null, 2)}\n`, "utf8");
  void vscode.window.showInformationMessage(`Vitte bench saved: ${outFile}`);
}

export function registerProjectAssistant(
  context: vscode.ExtensionContext,
  output: vscode.OutputChannel,
  getClient: () => LanguageClient | undefined,
  getActivationMs: () => number,
): void {
  context.subscriptions.push(
    doctorCollection,
    vscode.commands.registerCommand("vitte.projectAssistant", async () => {
      const pick = await vscode.window.showQuickPick(
        [
          { label: "New Project", command: "vitte.newProject" },
          { label: "New Module", command: "vitte.newModule" },
          { label: "New Package", command: "vitte.newPackage" },
          { label: "Generate .vscode/tasks+launch", command: "vitte.generateWorkspaceConfig" },
          { label: "Run Doctor", command: "vitte.doctor" },
        ],
        { placeHolder: "Vitte Project Assistant" }
      );
      if (!pick) return;
      await vscode.commands.executeCommand(pick.command);
    }),
    vscode.commands.registerCommand("vitte.newProject", createNewProject),
    vscode.commands.registerCommand("vitte.newModule", createNewModule),
    vscode.commands.registerCommand("vitte.newPackage", createNewPackage),
    vscode.commands.registerCommand("vitte.generateWorkspaceConfig", async () => {
      const roots = workspaceRoots();
      if (roots.length === 0) return;
      for (const ws of roots) await generateWorkspaceVscodeFiles(ws);
      void vscode.window.showInformationMessage(`Generated .vscode/tasks.json and launch.json for ${roots.length} root(s).`);
    }),
    vscode.commands.registerCommand("vitte.doctor", async () => runDoctor(output)),
    vscode.commands.registerCommand("vitte.doctor.runTool", async (tool?: string) => {
      const t = String(tool ?? "").toLowerCase();
      if (t === "check" || t === "lex" || t === "parse") {
        await runDoctor(output, t);
      }
    }),
    vscode.commands.registerCommand("vitte.benchExtensionCi", async () => {
      await runExtensionBench(getClient, output, getActivationMs());
    }),
  );
}
