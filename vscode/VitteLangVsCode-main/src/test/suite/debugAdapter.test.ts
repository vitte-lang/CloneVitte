import * as assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { VitteDebugAdapterDescriptorFactory } from '../../debug/adapterFactory';

interface DapMessage {
  type?: string;
  event?: string;
  command?: string;
  success?: boolean;
  body?: Record<string, unknown>;
  message?: string;
}

function debugSession(configuration: vscode.DebugConfiguration): vscode.DebugSession {
  return {
    id: 'vitte-debug-test',
    configuration,
  } as vscode.DebugSession;
}

function runNativeDapSmoke(): Promise<DapMessage[]> {
  const extensionRoot = path.resolve(__dirname, '../..');
  const repositoryRoot = path.resolve(__dirname, '../../../../..');
  const adapter = path.join(extensionRoot, 'debug/nativeAdapter.js');
  const compiler = process.env.VITTE_COMPILER ?? path.join(repositoryRoot, 'build/bin/vitte');
  const program = path.join(repositoryRoot, 'src/tests/grammar_alignment/let_mut_ok.vit');
  if (!existsSync(adapter) || !existsSync(compiler) || !existsSync(program)) {
    return Promise.reject(new Error('Native DAP smoke prerequisites are not available.'));
  }

  return new Promise<DapMessage[]>((resolve, reject) => {
    const child: ChildProcessWithoutNullStreams = spawn(process.execPath, [adapter], {
      cwd: repositoryRoot,
      env: { ...process.env, VITTE_COMPILER: compiler },
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    const messages: DapMessage[] = [];
    let buffer = Buffer.alloc(0);
    let sequence = 1;
    let scopeReference = 0;
    let topFrameId = 0;
    let stopped = false;
    let evaluated = false;
    let changed = false;
    let settled = false;

    const finish = (error?: Error): void => {
      if (settled) return;
      settled = true;
      if (!child.killed) child.kill('SIGTERM');
      if (error) reject(error);
      else resolve(messages);
    };
    const send = (command: string, args: Record<string, unknown> = {}): void => {
      const body = JSON.stringify({ seq: sequence++, type: 'request', command, arguments: args });
      child.stdin.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
    };
    const responseFor = (message: DapMessage, command: string): boolean => message.type === 'response' && message.command === command;

    const handle = (message: DapMessage): void => {
      messages.push(message);
      if (message.type === 'response' && message.success === false) {
        finish(new Error(`Native DAP ${message.command ?? 'request'} failed: ${message.message ?? 'unknown error'}`));
        return;
      }
      if (message.type === 'response' && responseFor(message, 'initialize')) {
        send('launch', { program, cwd: repositoryRoot, stopOnEntry: false });
      } else if (message.type === 'event' && message.event === 'initialized') {
        send('setBreakpoints', {
          source: { path: program },
          breakpoints: [
            { line: 47, logMessage: 'value={value}' },
            { line: 49, condition: 'value == 42' },
          ],
        });
      } else if (responseFor(message, 'setBreakpoints')) {
        const breakpoints = message.body?.breakpoints as Record<string, unknown>[] | undefined;
        assert.equal(breakpoints?.every((breakpoint) => breakpoint.verified === true), true);
        send('configurationDone');
      } else if (message.type === 'event' && message.event === 'stopped' && !stopped) {
        stopped = true;
        send('stackTrace', { threadId: 1, levels: 20 });
      } else if (responseFor(message, 'stackTrace')) {
        const stackFrames = message.body?.stackFrames as Record<string, unknown>[] | undefined;
        assert.ok((stackFrames?.length ?? 0) >= 3);
        assert.equal(stackFrames?.[0]?.line, 49);
        topFrameId = Number(stackFrames?.[0]?.id);
        send('scopes', { frameId: stackFrames?.[0]?.id });
      } else if (responseFor(message, 'scopes')) {
        const scopes = message.body?.scopes as Record<string, unknown>[] | undefined;
        scopeReference = Number(scopes?.[0]?.variablesReference);
        assert.ok(scopeReference > 0);
        send('variables', { variablesReference: scopeReference });
      } else if (responseFor(message, 'variables')) {
        const variables = message.body?.variables as Record<string, unknown>[] | undefined;
        assert.equal(variables?.find((item) => item.name === 'value')?.value, '42');
        send('evaluate', { frameId: topFrameId, expression: 'value + 1', context: 'watch' });
      } else if (responseFor(message, 'evaluate')) {
        assert.equal(message.body?.result, '43');
        evaluated = true;
        send('setVariable', { variablesReference: scopeReference, name: 'value', value: '43' });
      } else if (responseFor(message, 'setVariable')) {
        assert.equal(message.body?.value, '43');
        changed = true;
        send('disconnect');
      } else if (responseFor(message, 'disconnect')) {
        assert.equal(evaluated, true);
        assert.equal(changed, true);
        finish();
      }
    };

    child.stdout.on('data', (chunk: Buffer | string) => {
      buffer = Buffer.concat([buffer, Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)]);
      for (;;) {
        const separator = buffer.indexOf('\r\n\r\n');
        if (separator < 0) return;
        const header = buffer.subarray(0, separator).toString('ascii');
        const match = /Content-Length:\s*(\d+)/i.exec(header);
        if (!match) {
          buffer = buffer.subarray(separator + 4);
          continue;
        }
        const length = Number(match[1]);
        const bodyStart = separator + 4;
        if (buffer.length < bodyStart + length) return;
        const body = buffer.subarray(bodyStart, bodyStart + length).toString('utf8');
        buffer = buffer.subarray(bodyStart + length);
        try {
          handle(JSON.parse(body) as DapMessage);
        } catch (error) {
          finish(error instanceof Error ? error : new Error(String(error)));
          return;
        }
      }
    });
    child.stderr.on('data', () => undefined);
    child.once('error', (error) => finish(error));
    child.once('exit', (code) => {
      if (!settled && code !== 0) finish(new Error(`Native DAP exited with code ${code ?? 'unknown'}.`));
    });
    send('initialize', { adapterID: 'vitte', linesStartAt1: true, columnsStartAt1: true });
  });
}

suite('Vitte debug adapter', () => {
  const factory = new VitteDebugAdapterDescriptorFactory();

  test('stdio launch separates adapter args from debuggee args', async () => {
    const descriptor = await factory.createDebugAdapterDescriptor(debugSession({
      type: 'vitte',
      request: 'launch',
      name: 'Vitte Debug Test',
      adapterTransport: 'stdio',
      adapter: '/tools/vitte-dap',
      adapterArgs: ['--verbose'],
      program: '/workspace/game.vit',
      args: ['--level', '2'],
      cwd: '/workspace',
      env: { GAME_MODE: 'test' },
      adapterEnv: { DAP_TRACE: '1' },
    }));

    assert.ok(descriptor instanceof vscode.DebugAdapterExecutable);
    assert.equal(descriptor.command, '/tools/vitte-dap');
    assert.deepEqual(descriptor.args, ['--verbose']);
    const options = descriptor.options;
    assert.ok(options);
    assert.equal(options.cwd, '/workspace');
    assert.deepEqual(options.env, { DAP_TRACE: '1' });
  });

  test('native backend reads DWARF frames, locals, watches, and writes', async () => {
    const messages = await runNativeDapSmoke();
    assert.ok(messages.some((message) => message.type === 'event' && message.event === 'stopped'));
    assert.ok(messages.some((message) => message.type === 'response' && message.command === 'setVariable'));
  });

  test('stdio launch falls back to the bundled native adapter', async () => {
    const descriptor = await factory.createDebugAdapterDescriptor(debugSession({
      type: 'vitte',
      request: 'launch',
      name: 'Native Vitte Debug Test',
      nativeBackend: true,
      program: '/workspace/game.vit',
    }));

    assert.ok(descriptor instanceof vscode.DebugAdapterExecutable);
    assert.ok(descriptor.args.some((argument) => argument.endsWith('nativeAdapter.js')));
  });

  test('stdio launch can disable the native fallback explicitly', () => {
    assert.throws(
      () => factory.createDebugAdapterDescriptor(debugSession({
        type: 'vitte',
        request: 'launch',
        name: 'Disabled Native Vitte Debug Test',
        nativeBackend: false,
        program: '/workspace/game.vit',
      })),
      /No Vitte Debug Adapter Protocol executable is configured/,
    );
  });

  test('attach connects to the configured DAP TCP server', async () => {
    const descriptor = await factory.createDebugAdapterDescriptor(debugSession({
      type: 'vitte',
      request: 'attach',
      name: 'Vitte Attach Test',
      host: '127.0.0.2',
      port: 9333,
    }));

    assert.ok(descriptor instanceof vscode.DebugAdapterServer);
    assert.equal(descriptor.host, '127.0.0.2');
    assert.equal(descriptor.port, 9333);
  });

  test('attach rejects an invalid port instead of silently starting a runtime', () => {
    assert.throws(
      () => factory.createDebugAdapterDescriptor(debugSession({
        type: 'vitte',
        request: 'attach',
        name: 'Invalid Vitte Attach Test',
        host: '127.0.0.1',
        port: 65536,
      })),
      /port between 1 and 65535/,
    );
  });

  test('stdio launch rejects unsupported transport values', () => {
    assert.throws(
      () => factory.createDebugAdapterDescriptor(debugSession({
        type: 'vitte',
        request: 'launch',
        name: 'Invalid Vitte Launch Test',
        adapterTransport: 'serial',
        program: '/workspace/game.vit',
        args: ['--level', '2'],
      })),
      /Unsupported Vitte debug adapter transport/,
    );
  });
});
