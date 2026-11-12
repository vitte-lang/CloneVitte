import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';
import * as fs from 'fs';
import { locateVitteRuntime } from '../debug/runtimeLocator';

/**
 * Bench tasks & commands for Vitte
 *
 * Commands:
 *  - vitte.bench: Run benchmarks for the current workspace (profile/targets from settings or vitte.config.json)
 *  - vitte.benchReport: Run + open the latest HTML/SVG report if produced
 *  - vitte.openBenchDir: Reveal the bench report directory
 *
 * Task provider:
 *  - type: 'vitte' with command: 'bench' to integrate with VS Code Tasks
 */
export function registerBenchTasks(ctx: vscode.ExtensionContext) {
  // Commands
  ctx.subscriptions.push(
    vscode.commands.registerCommand('vitte.bench', async () => {
      await runBench(false);
    }),
    vscode.commands.registerCommand('vitte.benchReport', async () => {
      await runBench(true);
    }),
    vscode.commands.registerCommand('vitte.openBenchDir', async () => {
      const dir = await benchReportDir();
      if (dir) void vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(dir));
    }),
  );

  const provider = {
    provideTasks: async (_token?: vscode.CancellationToken): Promise<vscode.Task[]> => {
      const bin = await benchBin();
      if (!bin) return [];
      const definition: VitteBenchTaskDefinition = { type: 'vitte-bench', command: 'bench' };
      const exec = new vscode.ProcessExecution(bin, await benchArgs(bin));
      const task = new vscode.Task(
        definition,
        vscode.TaskScope.Workspace,
        'Vitte Bench',
        'vitte',
        exec
      );
      return [task];
    },
    resolveTask: (task: vscode.Task) => task,
  } as unknown as vscode.TaskProvider;
  ctx.subscriptions.push(vscode.tasks.registerTaskProvider('vitte-bench', provider));
}

interface VitteConfig {
  build?: {
    profile?: string;
    distributed?: boolean;
    incremental?: boolean;
  };
  bench?: {
    reportDir?: string;
    export?: unknown;
    regressionThreshold?: number;
  };
}

interface VitteBenchTaskDefinition extends vscode.TaskDefinition {
  type: 'vitte-bench';
  command: 'bench';
}

// ---- Helpers ----
async function benchBin(): Promise<string> {
  const located = await locateVitteRuntime();
  return located.benchPath ?? '';
}

async function readProjectConfig(): Promise<VitteConfig | undefined> {
  try {
    const files = await vscode.workspace.findFiles('vitte.config.json', '**/node_modules/**', 1);
    const uri = Array.isArray(files) ? files[0] : undefined;
    if (!uri) return undefined;
    const doc = await vscode.workspace.openTextDocument(uri);
    return JSON.parse(doc.getText()) as VitteConfig;
  } catch {
    return undefined;
  }
}

async function benchReportDir(): Promise<string | undefined> {
  const cfg = vscode.workspace.getConfiguration('vitte');
  const project = await readProjectConfig();
  const root = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!root) return undefined;
  const reportDir = (project?.bench?.reportDir ?? cfg.get<string>('bench.reportDir')) ?? '.vitte/bench';
  return path.isAbsolute(reportDir) ? reportDir : path.join(root, reportDir);
}

async function benchArgs(bin?: string): Promise<string[]> {
  const cfg = vscode.workspace.getConfiguration('vitte');
  const project = await readProjectConfig();

  const profile = project?.build?.profile ?? cfg.get<string>('build.profile') ?? 'bench';
  const distributed = (project?.build?.distributed ?? cfg.get<boolean>('build.distributed')) ?? false;
  const incremental = (project?.build?.incremental ?? cfg.get<boolean>('build.incremental')) ?? false;
  const reportDir = await benchReportDir();

  const args: string[] = path.basename(bin ?? '').startsWith('vitte-bench') ? ['--profile', profile] : ['bench', '--profile', profile];
  if (distributed) args.push('--distributed');
  if (incremental) args.push('--incremental');
  if (reportDir) args.push('--out', reportDir);

  // Export formats from config
  const exportKinds = project?.bench?.export;
  if (Array.isArray(exportKinds)) {
    for (const kind of exportKinds) {
      if (typeof kind === 'string') {
        args.push('--export', kind);
      }
    }
  }

  // Regression threshold
  if (typeof project?.bench?.regressionThreshold === 'number') {
    args.push('--threshold', String(project.bench.regressionThreshold));
  }

  return args;
}

async function benchCommand(): Promise<{ bin: string; args: string[] }> {
  const bin = await benchBin();
  const args = await benchArgs(bin);
  return { bin, args };
}

function formatCommandForDisplay(bin: string, args: string[]): string {
  return [bin, ...args].map((s) => JSON.stringify(s)).join(' ');
}

async function runBench(openAfter: boolean) {
  const root = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!root) {
    void vscode.window.showErrorMessage('Vitte: aucun workspace ouvert.');
    return;
  }

  const { bin, args } = await benchCommand();
  if (!bin) {
    void vscode.window.showInformationMessage('Vitte: aucun outil benchmark configuré pour ce projet.');
    return;
  }
  const outChan = vscode.window.createOutputChannel('Vitte Bench');

  await vscode.window.withProgress({
    location: vscode.ProgressLocation.Notification,
    title: 'Vitte: Benchmarks en cours…',
    cancellable: false,
  }, async () => {
    outChan.clear();
    outChan.show(true);
    outChan.appendLine(`[cmd] ${formatCommandForDisplay(bin, args)}`);

    await new Promise<void>((resolve) => {
      const proc = cp.spawn(bin, args, { cwd: root, shell: false, env: process.env });
      proc.stdout?.on('data', (b: Buffer) => outChan.append(b.toString()));
      proc.stderr?.on('data', (b: Buffer) => outChan.append(b.toString()));
      proc.on('error', (e) => {
        outChan.appendLine(`\n[error] ${e.message}`);
        resolve();
      });
      proc.on('close', (code) => {
        outChan.appendLine(`\n[exit] code=${code}`);
        resolve();
      });
    });
  });

  if (openAfter) await openLatestReport();
}

async function openLatestReport() {
  try {
    const dir = await benchReportDir();
    if (!dir) return;
    const files = await vscode.workspace.findFiles(new vscode.RelativePattern(dir, '*.{html,svg}'));
    if (!Array.isArray(files) || files.length === 0) {
      void vscode.window.showInformationMessage('Vitte Bench: aucun rapport HTML/SVG trouvé.');
      return;
    }
    // Sort by mtime (best-effort). Guard array access for noUncheckedIndexedAccess.
    const entries = files
      .map((u) => {
        try {
          return { uri: u, mtime: fs.statSync(u.fsPath).mtimeMs };
        } catch {
          return { uri: u, mtime: 0 };
        }
      })
      .sort((a, b) => b.mtime - a.mtime);

    const first = entries[0];
    if (!first) {
      void vscode.window.showInformationMessage('Vitte Bench: aucun rapport HTML/SVG trouvé.');
      return;
    }
    void vscode.commands.executeCommand('vscode.open', first.uri);
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    void vscode.window.showWarningMessage(`Vitte Bench: échec d'ouverture du rapport (${message})`);
  }
}
