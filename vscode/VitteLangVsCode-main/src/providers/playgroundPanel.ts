import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as cp from 'child_process';
import { locateVitteRuntime } from '../debug/runtimeLocator';

/**
 * Vitte Playground Panel
 * - Local, offline: edit a Vitte snippet, format it, run it via vitte.
 * - Stores temp files under ${workspace}/.vitte/playground/
 *
 * Command registration is centralized in src/extension.ts to avoid duplicates.
 */

export class PlaygroundPanel implements vscode.Disposable {
  private static current: PlaygroundPanel | undefined;
  private readonly panel: vscode.WebviewPanel;
  private readonly disposables: vscode.Disposable[] = [];
  private terminal: vscode.Terminal | undefined;
  private lastFile: string | undefined;

  static createOrShow(ctx: vscode.ExtensionContext) {
    const column = vscode.window.activeTextEditor?.viewColumn ?? vscode.ViewColumn.Two;
    if (PlaygroundPanel.current) {
      PlaygroundPanel.current.panel.reveal(column);
      return;
    }
    const panel = vscode.window.createWebviewPanel(
      'vittePlayground',
      'Vitte • Playground',
      column,
      {
        enableScripts: true,
        retainContextWhenHidden: true,
        enableFindWidget: true,
      }
    );
    PlaygroundPanel.current = new PlaygroundPanel(ctx, panel);
  }

  private constructor(private readonly ctx: vscode.ExtensionContext, panel: vscode.WebviewPanel) {
    this.panel = panel;
    this.panel.onDidDispose(() => this.dispose(), null, this.disposables);
    this.panel.webview.onDidReceiveMessage((m) => this.onMessage(m), null, this.disposables);
    void this.render();
  }

  dispose(): void {
    PlaygroundPanel.current = undefined;
    try { this.terminal?.dispose(); } catch {}
    while (this.disposables.length) this.disposables.pop()?.dispose();
  }

  // ---- Messaging ----
  private async onMessage(msg: unknown) {
    if (!msg || typeof msg !== 'object') return;
    const m = msg as Partial<{ type: string; text: unknown; save: unknown }>;
    switch (m.type) {
      case 'format': {
        const text = typeof m.text === 'string' ? m.text : '';
        const formatted = await this.formatCode(text);
        this.post({ type: 'formatted', text: formatted ?? text });
        return;
      }
      case 'run': {
        const text = typeof m.text === 'string' ? m.text : '';
        const save = Boolean(m.save);
        await this.runSnippet(text, save);
        return;
      }
      case 'save': {
        const text = typeof m.text === 'string' ? m.text : '';
        const file = await this.saveSnippet(text);
        this.post({ type: 'saved', file });
        return;
      }
      case 'openFolder': {
        const folder = await this.ensurePlaygroundDir();
        if (folder) void vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(folder));
        return;
      }
      default:
        return;
    }
  }

  private post(payload: unknown) {
    void this.panel.webview.postMessage(payload);
  }

  // ---- Tooling helpers ----
  private async getTooling(): Promise<{ cli: string; fmt?: string }> {
    const located = await locateVitteRuntime();
    const cli = located.cliPath ?? located.runtimePath ?? 'vitte';
    const out: { cli: string; fmt?: string } = { cli };
    if (typeof located.fmtPath === 'string') out.fmt = located.fmtPath;
    return out;
  }

  private async ensurePlaygroundDir(): Promise<string | undefined> {
    const wf = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!wf) return undefined;
    const dir = path.join(wf, '.vitte', 'playground');
    try { await fs.promises.mkdir(dir, { recursive: true }); } catch { /* ignore */ }
    return dir;
  }

  private async saveSnippet(text: string): Promise<string | undefined> {
    const dir = await this.ensurePlaygroundDir();
    if (!dir) return undefined;
    const file = path.join(dir, 'main.vit');
    await fs.promises.writeFile(file, text, 'utf8');
    this.lastFile = file;
    return file;
  }

  private async formatCode(text: string): Promise<string | undefined> {
    try {
      const { fmt } = await this.getTooling();
      if (!fmt) return undefined;
      const fmtArgs = path.basename(fmt).startsWith('vitte-fmt') ? [] : ['fmt', '-'];
      return await new Promise<string>((resolve, reject) => {
        const p = cp.spawn(fmt, fmtArgs, { stdio: ['pipe', 'pipe', 'pipe'] });
        let out = '';
        let err = '';
        p.stdout.on('data', (b: Buffer) => { out += b.toString(); });
        p.stderr.on('data', (b: Buffer) => { err += b.toString(); });
        p.on('error', reject);
        p.on('close', (code) => {
          if (code === 0) resolve(out);
          else reject(new Error(err || `vitte fmt exited with ${code}`));
        });
        if (p.stdin) {
          p.stdin.write(text);
          p.stdin.end();
        } else {
          reject(new Error('vitte fmt stdin unavailable'));
        }
      });
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      void vscode.window.showWarningMessage(`Format: ${message}`);
      return undefined;
    }
  }

  private async runSnippet(text: string, saveFirst: boolean) {
    const file = saveFirst ? (await this.saveSnippet(text)) : (this.lastFile ?? await this.saveSnippet(text));
    if (!file) {
      void vscode.window.showErrorMessage('Playground: impossible d’écrire le fichier.');
      return;
    }

    const { cli } = await this.getTooling();
    const wf = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? path.dirname(file);
    const cmd = `${quote(cli)} run ${quote(file)}`;

    // Ensure terminal
    this.terminal ??= vscode.window.createTerminal({ name: 'Vitte Playground', cwd: wf });
    this.terminal.show(true);
    this.terminal.sendText(cmd, true);
  }

  // ---- Render ----
  private render() {
    const webview = this.panel.webview;
    const nonce = String(Date.now());
    const csp = `default-src 'none'; style-src 'unsafe-inline' ${webview.cspSource}; script-src 'nonce-${nonce}';`;
    const initial = `// Editez votre code Vitte ici\nproc main() -> int {\n  return 0;\n}`;

    this.panel.webview.html = `<!DOCTYPE html>
      <html lang="fr">
      <head>
        <meta charset="UTF-8" />
        <meta http-equiv="Content-Security-Policy" content="${csp}">
        <meta name="viewport" content="width=device-width, initial-scale=1" />
        <title>Vitte • Playground</title>
        <style>
          :root { --pad: 12px; }
          body { font: 13px/1.5 -apple-system, Segoe UI, system-ui, sans-serif; padding: 0; margin: 0; }
          header { position: sticky; top: 0; backdrop-filter: blur(8px); background: var(--vscode-editor-background); border-bottom: 1px solid var(--vscode-panel-border); padding: 10px var(--pad); display:flex; gap:10px; align-items:center; }
          .btn { padding:6px 10px; border-radius:6px; border:1px solid var(--vscode-button-border, transparent); background: var(--vscode-button-secondaryBackground); color: var(--vscode-button-secondaryForeground); cursor:pointer; }
          .btn.primary { background: var(--vscode-button-background); color: var(--vscode-button-foreground); }
          textarea { width: 100%; height: calc(100vh - 120px); box-sizing: border-box; padding: var(--pad); border: none; outline: none; resize: none; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace; font-size: 12.5px; background: var(--vscode-editor-background); color: var(--vscode-editor-foreground); }
          footer { padding: 8px var(--pad); color: var(--vscode-descriptionForeground); display: flex; justify-content: space-between; align-items:center; }
        </style>
      </head>
      <body>
        <header>
          <button id="format" class="btn">Formater</button>
          <button id="run" class="btn primary">Exécuter ▶</button>
          <button id="save" class="btn">Enregistrer</button>
          <button id="open" class="btn">Ouvrir le dossier</button>
          <span id="status"></span>
        </header>
        <textarea id="code" spellcheck="false">${escapeHtml(initial)}</textarea>
        <footer>
          <span>Les sorties d’exécution s’affichent dans le terminal intégré “Vitte Playground”.</span>
          <span id="info"></span>
        </footer>
        <script nonce="${nonce}">
          const vscodeApi = acquireVsCodeApi();
          const $ = (sel) => document.querySelector(sel);
          const code = $('#code');
          const status = $('#status');
          const info = $('#info');

          function post(type, payload) { vscodeApi.postMessage({ type, ...payload }); }

          $('#format').addEventListener('click', () => { status.textContent = 'Formatage…'; post('format', { text: code.value }); });
          $('#run').addEventListener('click', () => { status.textContent = 'Exécution…'; post('run', { text: code.value, save: true }); });
          $('#save').addEventListener('click', () => { post('save', { text: code.value }); });
          $('#open').addEventListener('click', () => { post('openFolder', {}); });

          window.addEventListener('message', (e) => {
            const m = e.data || {};
            if (m.type === 'formatted') {
              code.value = m.text || code.value;
              status.textContent = 'Formatté.';
              setTimeout(() => status.textContent = '', 800);
            }
            if (m.type === 'saved') {
              info.textContent = m.file ? 'Enregistré: ' + m.file : '';
              status.textContent = m.file ? 'Enregistré.' : 'Erreur sauvegarde';
              setTimeout(() => status.textContent = '', 1000);
            }
          });
        </script>
      </body>
      </html>`;
  }
}

export default PlaygroundPanel;

const HTML_ESCAPE: Record<string, string> = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
  '\'': '&#39;',
};

function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (c) => HTML_ESCAPE[c] ?? c);
}

function quote(p: string): string {
  if (process.platform === 'win32') return `"${p.replace(/"/g, '\\"')}"`;
  return `'${p.replace(/'/g, `'\\''`)}'`;
}
