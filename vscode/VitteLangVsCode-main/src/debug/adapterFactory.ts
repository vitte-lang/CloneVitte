import * as fs from "node:fs";
import { workspaceBinaryRoots, expandToolPath } from "../utils/project";
import * as vscode from 'vscode';
import * as path from 'node:path';

type DebugTransport = 'stdio' | 'tcp';

function readString(value: unknown): string | undefined {
  return typeof value === 'string' && value.trim().length > 0 ? value.trim() : undefined;
}

function readStringArray(value: unknown): string[] | undefined {
  if (!Array.isArray(value) || !value.every((item): item is string => typeof item === 'string')) {
    return undefined;
  }
  return value;
}

function readBoolean(value: unknown): boolean | undefined {
  return typeof value === 'boolean' ? value : undefined;
}

function readEnvironment(value: unknown): Record<string, string> | undefined {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return undefined;
  const entries = Object.entries(value).filter((entry): entry is [string, string] => typeof entry[1] === 'string');
  return entries.length > 0 ? Object.fromEntries(entries) : undefined;
}

export class VitteDebugAdapterDescriptorFactory implements vscode.DebugAdapterDescriptorFactory {
  createDebugAdapterDescriptor(session: vscode.DebugSession): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    const config = session.configuration as Record<string, unknown>;
    const request = config.request;
    const settings = vscode.workspace.getConfiguration('vitte');
    const transport = this.getTransport(config, settings, request);

    if (transport === 'tcp') {
      const port = config.port;
      if (typeof port !== 'number' || !Number.isInteger(port) || port < 1 || port > 65535) {
        throw new Error('Vitte DAP TCP transport requires a port between 1 and 65535.');
      }
      const host = readString(config.host) ?? '127.0.0.1';
      return new vscode.DebugAdapterServer(port, host);
    }

    if (request !== 'launch') {
      throw new Error('Vitte DAP stdio transport supports launch requests only; use TCP for attach.');
    }

    const adapter = this.resolveAdapter(config, settings);

    const adapterArgs = readStringArray(config.adapterArgs)
      ?? readStringArray(settings.get('debug.adapterArgs'))
      ?? [];
    const cwd = readString(config.adapterCwd) ?? readString(config.cwd)
      ?? vscode.workspace.workspaceFolders?.[0]?.uri.fsPath
      ?? process.cwd();
    const env = readEnvironment(config.adapterEnv);
    const options: vscode.DebugAdapterExecutableOptions = { cwd };
    if (env) options.env = env;

    if (!adapter) {
      const nativeEnabled = readBoolean(config.nativeBackend)
        ?? readBoolean(settings.get('debug.nativeBackend'))
        ?? true;
      if (!nativeEnabled) {
        throw new Error(
          'No Vitte Debug Adapter Protocol executable is configured. Set "vitte.debug.program" to a DAP adapter, or enable "vitte.debug.nativeBackend".',
        );
      }

      const nativeAdapter = path.join(__dirname, 'nativeAdapter.js');
      const nodePath = readString(config.nodePath)
        ?? readString(settings.get('debug.nodePath'))
        ?? process.env.VITTE_NODE_PATH
        ?? process.execPath;
      const nodeArgs = process.env.VITTE_NODE_PATH || readString(config.nodePath)
        || readString(settings.get('debug.nodePath'))
        ? [nativeAdapter]
        : ['--ms-enable-electron-run-as-node', nativeAdapter];
      const explicitCompiler = readString(config.compiler) ?? readString(settings.get('compiler.path'));
      const compiler = explicitCompiler ? expandToolPath(explicitCompiler, cwd) : workspaceBinaryRoots([cwd])
        .map(root => path.join(root, process.platform === 'win32' ? 'vitte.exe' : 'vitte'))
        .find(candidate => fs.existsSync(candidate));
      const nativeEnv: Record<string, string> = { ...(options.env ?? {}) };
      if (compiler && !nativeEnv.VITTE_COMPILER) nativeEnv.VITTE_COMPILER = compiler;
      options.env = nativeEnv;
      return new vscode.DebugAdapterExecutable(nodePath, nodeArgs, options);
    }

    return new vscode.DebugAdapterExecutable(adapter, adapterArgs, options);
  }

  private getTransport(
    config: Record<string, unknown>,
    settings: vscode.WorkspaceConfiguration,
    request: unknown,
  ): DebugTransport {
    if (request === 'attach') return 'tcp';

    const configured = config.adapterTransport
      ?? settings.get('debug.adapterTransport')
      ?? 'stdio';
    if (configured === 'stdio' || configured === 'tcp') return configured;
    if (typeof configured === 'string') {
      throw new Error(`Unsupported Vitte debug adapter transport "${configured}". Use "stdio" or "tcp".`);
    }
    throw new Error('Vitte debug adapter transport must be "stdio" or "tcp".');
  }

  private resolveAdapter(
    config: Record<string, unknown>,
    settings: vscode.WorkspaceConfiguration,
  ): string | undefined {
    const configured = readString(config.adapter)
      ?? readString(settings.get('debug.program'))
      ?? readString(settings.get('runtime.path'));
    if (!configured) return undefined;

    if (path.isAbsolute(configured)) return configured;
    const toolchainRoot = readString(settings.get('toolchain.root'))
      ?? readString(settings.get('toolchainPath'));
    return toolchainRoot ? path.join(toolchainRoot, configured) : configured;
  }
}

export function registerDebugFactory(ctx: vscode.ExtensionContext): void {
  const factory = new VitteDebugAdapterDescriptorFactory();
  ctx.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('vitte', factory));
}
