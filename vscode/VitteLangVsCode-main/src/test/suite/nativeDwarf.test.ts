import * as assert from 'node:assert/strict';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync, spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import * as path from 'node:path';
import * as os from 'node:os';

interface DapMessage {
  type?: string;
  event?: string;
  command?: string;
  success?: boolean;
  body?: Record<string, unknown>;
  message?: string;
}

interface BreakpointRequest {
  line: number;
  condition?: string;
  hitCondition?: string;
  logMessage?: string;
}

interface DapScenario {
  program: string;
  args?: string[];
  stopOnEntry?: boolean;
  breakpoints?: (number | BreakpointRequest)[];
  expectedFailures?: string[];
  onMessage: (message: DapMessage, send: (command: string, args?: Record<string, unknown>) => void, finish: (error?: Error) => void) => void;
}

function runNativeDapScenario(scenario: DapScenario): Promise<DapMessage[]> {
  const extensionRoot = path.resolve(__dirname, '../..');
  const repositoryRoot = path.resolve(__dirname, '../../../../..');
  const adapter = path.join(extensionRoot, 'debug/nativeAdapter.js');
  const compiler = process.env.VITTE_COMPILER ?? path.join(repositoryRoot, 'build/bin/vitte');
  if (!existsSync(adapter) || !existsSync(compiler) || !existsSync(scenario.program)) {
    return Promise.reject(new Error('Native DAP DWARF test prerequisites are not available.'));
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
    let settled = false;
    const timeout = setTimeout(() => finish(new Error('Native DAP DWARF scenario timed out.')), 20_000);

    const finish = (error?: Error): void => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      if (!child.killed) child.kill('SIGTERM');
      if (error) reject(error);
      else resolve(messages);
    };

    const send = (command: string, args: Record<string, unknown> = {}): void => {
      const body = JSON.stringify({ seq: sequence++, type: 'request', command, arguments: args });
      child.stdin.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
    };

    const handle = (message: DapMessage): void => {
      messages.push(message);
      if (message.type === 'response' && message.success === false && !scenario.expectedFailures?.includes(message.command ?? '')) {
        finish(new Error(`Native DAP ${message.command ?? 'request'} failed: ${message.message ?? 'unknown error'}`));
        return;
      }
      if (message.type === 'response' && message.command === 'initialize') {
        send('launch', {
          program: scenario.program,
          args: scenario.args ?? [],
          cwd: repositoryRoot,
          stopOnEntry: scenario.stopOnEntry ?? false,
        });
      } else if (message.type === 'event' && message.event === 'initialized') {
        if (scenario.breakpoints && scenario.breakpoints.length > 0) {
          send('setBreakpoints', {
            source: { path: scenario.program },
            breakpoints: scenario.breakpoints.map((breakpoint) => (
              typeof breakpoint === 'number' ? { line: breakpoint } : breakpoint
            )),
          });
        } else {
          send('configurationDone');
        }
      } else if (message.type === 'response' && message.command === 'setBreakpoints') {
        send('configurationDone');
      }
      try {
        scenario.onMessage(message, send, finish);
      } catch (error) {
        finish(error instanceof Error ? error : new Error(String(error)));
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

function responseBody(message: DapMessage, command: string): Record<string, unknown> {
  assert.equal(message.type, 'response');
  assert.equal(message.command, command);
  return message.body ?? {};
}

suite('Vitte native DWARF debugger', () => {
  test('stopOnEntry stops at the real Vitte entry frame', async () => {
    const repositoryRoot = path.resolve(__dirname, '../../../../..');
    const program = path.join(repositoryRoot, 'src/tests/debug/scopes_arrays.vit');
    let state = 'waiting-for-entry';

    const messages = await runNativeDapScenario({
      program,
      stopOnEntry: true,
      onMessage: (message, send, finish) => {
        if (message.type === 'event' && message.event === 'stopped' && state === 'waiting-for-entry') {
          assert.equal(message.body?.reason, 'entry');
          state = 'waiting-for-stack';
          send('stackTrace', { threadId: 1, levels: 8 });
        } else if (message.type === 'response' && message.command === 'stackTrace' && state === 'waiting-for-stack') {
          const stackFrames = responseBody(message, 'stackTrace').stackFrames as Record<string, unknown>[] | undefined;
          const top = stackFrames?.[0];
          assert.ok(top);
          assert.match(String(top?.name), /vitte_entry_main/);
          assert.equal((top?.source as Record<string, unknown> | undefined)?.path, program);
          assert.ok(Number(top?.line) > 0);
          state = 'waiting-for-disconnect';
          send('disconnect');
        } else if (message.type === 'response' && message.command === 'disconnect' && state === 'waiting-for-disconnect') {
          finish();
        }
      },
    });

    assert.ok(messages.some((message) => message.type === 'event' && message.event === 'stopped'));
  });

  test('models shadowing, lifetimes, nested arrays, and pointers', async () => {
    const repositoryRoot = path.resolve(__dirname, '../../../../..');
    const program = path.join(repositoryRoot, 'src/tests/debug/scopes_arrays.vit');
    let state = 'waiting-for-inspect-stop';
    let scopeReference = 0;
    let containerReference = 0;
    let pointsReference = 0;
    let pointElementReference = 0;
    let pointReference = 0;
    let inspectFrameId = 0;
    let mainFrameId = 0;
    let staleScopeReference = 0;
    const parallelWatchResults = new Set<string>();
    let parallelWatchResponses = 0;

    const variablesOf = (message: DapMessage): Record<string, unknown>[] => (
      (responseBody(message, 'variables').variables as Record<string, unknown>[] | undefined) ?? []
    );

    const messages = await runNativeDapScenario({
      program,
      breakpoints: [21, 47, 53],
      expectedFailures: ['evaluate', 'setVariable'],
      onMessage: (message, send, finish) => {
        if (message.type === 'event' && message.event === 'stopped' && state === 'waiting-for-inspect-stop') {
          assert.equal(message.body?.reason, 'breakpoint');
          state = 'waiting-for-inspect-stack';
          send('stackTrace', { threadId: 1, levels: 8 });
        } else if (message.type === 'response' && message.command === 'stackTrace' && state === 'waiting-for-inspect-stack') {
          const stackFrames = responseBody(message, 'stackTrace').stackFrames as Record<string, unknown>[] | undefined;
          assert.equal(stackFrames?.[0]?.line, 21);
          assert.equal(stackFrames?.[0]?.name, 'inspect');
          inspectFrameId = Number(stackFrames?.[0]?.id);
          mainFrameId = Number(stackFrames?.find((frame) => frame.name === 'vitte_entry_main')?.id);
          assert.ok(mainFrameId > inspectFrameId);
          state = 'waiting-for-caller-scopes';
          send('scopes', { frameId: mainFrameId });
        } else if (message.type === 'response' && message.command === 'scopes' && state === 'waiting-for-caller-scopes') {
          const scopes = responseBody(message, 'scopes').scopes as Record<string, unknown>[] | undefined;
          state = 'waiting-for-caller-locals';
          send('variables', { variablesReference: Number(scopes?.[0]?.variablesReference) });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-caller-locals') {
          assert.equal(variablesOf(message).find((item) => item.name === 'value')?.value, '20');
          state = 'waiting-for-parallel-watches';
          send('evaluate', { frameId: mainFrameId, expression: 'value', context: 'watch' });
          send('evaluate', { frameId: inspectFrameId, expression: 'local', context: 'watch' });
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-parallel-watches') {
          parallelWatchResponses += 1;
          parallelWatchResults.add(String(responseBody(message, 'evaluate').result));
          if (parallelWatchResponses === 2) {
            assert.deepEqual(parallelWatchResults, new Set(['20', '5']));
            state = 'waiting-for-inspect-scopes';
            send('scopes', { frameId: inspectFrameId });
          }
        } else if (message.type === 'response' && message.command === 'scopes' && state === 'waiting-for-inspect-scopes') {
          const scopes = responseBody(message, 'scopes').scopes as Record<string, unknown>[] | undefined;
          scopeReference = Number(scopes?.[0]?.variablesReference);
          assert.ok(scopeReference > 0);
          state = 'waiting-for-inspect-locals';
          send('variables', { variablesReference: scopeReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-inspect-locals') {
          const variables = variablesOf(message);
          const container = variables.find((item) => item.name === 'container');
          const point = variables.find((item) => item.name === 'point');
          assert.ok(container && String(container.type).includes('Container'));
          assert.equal(variables.find((item) => item.name === 'local')?.value, '5');
          containerReference = Number(container?.variablesReference);
          pointReference = Number(point?.variablesReference);
          assert.ok(containerReference > 0);
          assert.ok(pointReference > 0);
          state = 'waiting-for-container';
          send('variables', { variablesReference: containerReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-container') {
          const variables = variablesOf(message);
          const points = variables.find((item) => item.name === 'points');
          assert.equal(variables.find((item) => item.name === 'marker')?.value, '99');
          pointsReference = Number(points?.variablesReference);
          assert.ok(pointsReference > 0);
          state = 'waiting-for-points';
          send('variables', { variablesReference: pointsReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-points') {
          const variables = variablesOf(message);
          const element = variables.find((item) => item.name === '[1]');
          assert.ok(element);
          pointElementReference = Number(element?.variablesReference);
          assert.ok(pointElementReference > 0);
          state = 'waiting-for-point-element';
          send('variables', { variablesReference: pointElementReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-point-element') {
          const variables = variablesOf(message);
          assert.equal(variables.find((item) => item.name === 'x')?.value, '20');
          assert.equal(variables.find((item) => item.name === 'y')?.value, '22');
          state = 'waiting-for-array-write';
          send('setVariable', { variablesReference: pointElementReference, name: 'x', value: '23' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-array-write') {
          assert.equal(responseBody(message, 'setVariable').value, '23');
          state = 'waiting-for-array-restore';
          send('setVariable', { variablesReference: pointElementReference, name: 'x', value: '20' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-array-restore') {
          assert.equal(responseBody(message, 'setVariable').value, '20');
          state = 'waiting-for-pointer';
          send('variables', { variablesReference: pointReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-pointer') {
          const variables = variablesOf(message);
          assert.equal(variables.find((item) => item.name === 'x')?.value, '20');
          assert.equal(variables.find((item) => item.name === 'y')?.value, '22');
          state = 'waiting-for-pointer-watch';
          send('evaluate', { frameId: inspectFrameId, expression: 'container.points[1].x + point->x + local', context: 'watch' });
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-pointer-watch') {
          assert.equal(responseBody(message, 'evaluate').result, '45');
          state = 'waiting-for-pointer-write';
          send('setVariable', { variablesReference: pointReference, name: 'x', value: '23' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-pointer-write') {
          assert.equal(responseBody(message, 'setVariable').value, '23');
          state = 'waiting-for-pointer-restore';
          send('setVariable', { variablesReference: pointReference, name: 'x', value: '20' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-pointer-restore') {
          assert.equal(responseBody(message, 'setVariable').value, '20');
          state = 'waiting-for-shadow-stop';
          send('continue');
        } else if (message.type === 'event' && message.event === 'stopped' && state === 'waiting-for-shadow-stop') {
          assert.equal(message.body?.reason, 'breakpoint');
          staleScopeReference = scopeReference;
          state = 'waiting-for-shadow-stack';
          send('stackTrace', { threadId: 1, levels: 8 });
        } else if (message.type === 'response' && message.command === 'stackTrace' && state === 'waiting-for-shadow-stack') {
          const stackFrames = responseBody(message, 'stackTrace').stackFrames as Record<string, unknown>[] | undefined;
          assert.equal(stackFrames?.[0]?.line, 47);
          const shadowFrameId = Number(stackFrames?.[0]?.id);
          assert.notEqual(shadowFrameId, inspectFrameId);
          state = 'waiting-for-stale-frame';
          send('evaluate', { frameId: inspectFrameId, expression: 'local', context: 'watch' });
          inspectFrameId = shadowFrameId;
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-stale-frame') {
          assert.equal(message.success, false);
          assert.match(String(message.message), /no longer available/i);
          state = 'waiting-for-shadow-scopes';
          send('scopes', { frameId: inspectFrameId });
        } else if (message.type === 'response' && message.command === 'scopes' && state === 'waiting-for-shadow-scopes') {
          const scopes = responseBody(message, 'scopes').scopes as Record<string, unknown>[] | undefined;
          scopeReference = Number(scopes?.[0]?.variablesReference);
          assert.notEqual(scopeReference, staleScopeReference);
          state = 'waiting-for-stale-locals';
          send('variables', { variablesReference: staleScopeReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-stale-locals') {
          assert.deepEqual(variablesOf(message), []);
          state = 'waiting-for-shadow-locals';
          send('variables', { variablesReference: scopeReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-shadow-locals') {
          const variables = variablesOf(message);
          assert.equal(variables.find((item) => item.name === 'value')?.value, '20');
          assert.equal(variables.find((item) => item.name === 'inner')?.value, '21');
          assert.equal(variables.find((item) => item.name === 'total')?.value, '45');
          assert.equal(variables.some((item) => item.name === 'after'), false);
          state = 'waiting-for-future-watch';
          send('evaluate', { frameId: inspectFrameId, expression: 'after', context: 'watch' });
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-future-watch') {
          assert.equal(message.success, false);
          state = 'waiting-for-future-write';
          send('setVariable', { variablesReference: scopeReference, name: 'after', value: '999' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-future-write') {
          assert.equal(message.success, false);
          state = 'waiting-for-after-stop';
          send('continue');
        } else if (message.type === 'event' && message.event === 'stopped' && state === 'waiting-for-after-stop') {
          assert.equal(message.body?.reason, 'breakpoint');
          state = 'waiting-for-after-stack';
          send('stackTrace', { threadId: 1, levels: 8 });
        } else if (message.type === 'response' && message.command === 'stackTrace' && state === 'waiting-for-after-stack') {
          const stackFrames = responseBody(message, 'stackTrace').stackFrames as Record<string, unknown>[] | undefined;
          assert.equal(stackFrames?.[0]?.line, 53);
          assert.notEqual(stackFrames?.[0]?.id, inspectFrameId);
          state = 'waiting-for-after-scopes';
          send('scopes', { frameId: stackFrames?.[0]?.id });
        } else if (message.type === 'response' && message.command === 'scopes' && state === 'waiting-for-after-scopes') {
          const scopes = responseBody(message, 'scopes').scopes as Record<string, unknown>[] | undefined;
          scopeReference = Number(scopes?.[0]?.variablesReference);
          state = 'waiting-for-after-locals';
          send('variables', { variablesReference: scopeReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-after-locals') {
          const variables = variablesOf(message);
          assert.equal(variables.find((item) => item.name === 'value')?.value, '10');
          assert.equal(variables.find((item) => item.name === 'after')?.value, '11');
          assert.equal(variables.some((item) => item.name === 'inner'), false);
          assert.equal(variables.some((item) => item.name === 'total'), false);
          state = 'waiting-for-disconnect';
          send('disconnect');
        } else if (message.type === 'response' && message.command === 'disconnect' && state === 'waiting-for-disconnect') {
          finish();
        }
      },
    });

    assert.ok(messages.some((message) => message.type === 'event' && message.event === 'stopped'));
  });

  test('applies conditional breakpoints and repeated logpoints', async () => {
    const repositoryRoot = path.resolve(__dirname, '../../../../..');
    const program = path.join(repositoryRoot, 'src/tests/debug/conditional_logpoints.vit');
    const logs: string[] = [];
    let conditionalStop = false;
    let state = 'running';

    const messages = await runNativeDapScenario({
      program,
      breakpoints: [
        { line: 10, condition: 'index == 2' },
        { line: 11, logMessage: 'index={index} total={total}' },
      ],
      onMessage: (message, send, finish) => {
        if (message.type === 'event' && message.event === 'output' && message.body?.category === 'console') {
          const text = typeof message.body.output === 'string' ? message.body.output.trim() : '';
          if (/^index=\d+ total=\d+$/.test(text)) logs.push(text);
        } else if (message.type === 'event' && message.event === 'stopped') {
          assert.equal(message.body?.reason, 'breakpoint');
          assert.equal(conditionalStop, false);
          conditionalStop = true;
          state = 'waiting-for-conditional-stack';
          send('stackTrace', { threadId: 1, levels: 4 });
        } else if (message.type === 'response' && message.command === 'stackTrace' && state === 'waiting-for-conditional-stack') {
          const stackFrames = responseBody(message, 'stackTrace').stackFrames as Record<string, unknown>[] | undefined;
          assert.equal(stackFrames?.[0]?.line, 10);
          state = 'running';
          send('continue');
        } else if (message.type === 'event' && message.event === 'terminated') {
          assert.equal(conditionalStop, true);
          assert.deepEqual(logs, [
            'index=0 total=0',
            'index=1 total=1',
            'index=2 total=3',
            'index=3 total=6',
          ]);
          finish();
        }
      },
    });

    assert.ok(messages.some((message) => message.type === 'event' && message.event === 'terminated'));
  });

  test('reads nested aggregates, strings, floats, scopes, and expressions', async () => {
    const repositoryRoot = path.resolve(__dirname, '../../../../..');
    const program = path.join(repositoryRoot, 'src/tests/debug/dwarf_values.vit');
    let state = 'waiting-for-stop';
    let scopeReference = 0;
    let snapshotReference = 0;
    let pointReference = 0;
    let topFrameId = 0;
    let nestedExpressionChecked = false;
    let changed = false;

    const messages = await runNativeDapScenario({
      program,
      breakpoints: [34],
      expectedFailures: ['evaluate', 'setVariable'],
      onMessage: (message, send, finish) => {
        if (message.type === 'event' && message.event === 'stopped' && state === 'waiting-for-stop') {
          assert.equal(message.body?.reason, 'breakpoint');
          state = 'waiting-for-stack';
          send('stackTrace', { threadId: 1, levels: 20 });
        } else if (message.type === 'response' && message.command === 'stackTrace' && state === 'waiting-for-stack') {
          const body = responseBody(message, 'stackTrace');
          const stackFrames = body.stackFrames as Record<string, unknown>[] | undefined;
          assert.ok((stackFrames?.length ?? 0) >= 3);
          assert.equal(stackFrames?.[0]?.line, 34);
          topFrameId = Number(stackFrames?.[0]?.id);
          state = 'waiting-for-scopes';
          send('scopes', { frameId: stackFrames?.[0]?.id });
        } else if (message.type === 'response' && message.command === 'scopes' && state === 'waiting-for-scopes') {
          const scopes = responseBody(message, 'scopes').scopes as Record<string, unknown>[] | undefined;
          scopeReference = Number(scopes?.[0]?.variablesReference);
          assert.ok(scopeReference > 0);
          state = 'waiting-for-locals';
          send('variables', { variablesReference: scopeReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-locals') {
          const variables = responseBody(message, 'variables').variables as Record<string, unknown>[] | undefined;
          const snapshot = variables?.find((item) => item.name === 'snapshot');
          assert.equal(snapshot?.type, 'Snapshot');
          assert.match(String(snapshot?.value), /\{/);
          snapshotReference = Number(snapshot?.variablesReference);
          assert.ok(snapshotReference > 0);
          assert.equal(variables?.find((item) => item.name === 'extra')?.value, '1');
          state = 'waiting-for-aggregate';
          send('variables', { variablesReference: snapshotReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-aggregate') {
          const variables = responseBody(message, 'variables').variables as Record<string, unknown>[] | undefined;
          const point = variables?.find((item) => item.name === 'point');
          pointReference = Number(point?.variablesReference);
          assert.ok(pointReference > 0);
          assert.ok(variables?.some((item) => item.name === 'label' && String(item.value).includes('dwarf')));
          assert.equal(variables?.find((item) => item.name === 'ratio')?.value, '1.5');
          state = 'waiting-for-point';
          send('variables', { variablesReference: pointReference });
        } else if (message.type === 'response' && message.command === 'variables' && state === 'waiting-for-point') {
          const variables = responseBody(message, 'variables').variables as Record<string, unknown>[] | undefined;
          assert.equal(variables?.find((item) => item.name === 'x')?.value, '20');
          assert.equal(variables?.find((item) => item.name === 'y')?.value, '22');
          state = 'waiting-for-expression';
          send('evaluate', {
            frameId: topFrameId,
            expression: 'snapshot.point.x + snapshot.point.y + extra',
            context: 'watch',
          });
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-expression') {
          assert.equal(message.success, true);
          assert.equal(responseBody(message, 'evaluate').result, '43');
          nestedExpressionChecked = true;
          state = 'waiting-for-write';
          send('setVariable', { variablesReference: scopeReference, name: 'extra', value: '2' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-write') {
          assert.equal(message.success, true);
          assert.equal(responseBody(message, 'setVariable').value, '2');
          changed = true;
          state = 'waiting-for-nested-write';
          send('setVariable', { variablesReference: pointReference, name: 'x', value: '21' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-nested-write') {
          assert.equal(message.success, true);
          assert.equal(responseBody(message, 'setVariable').value, '21');
          state = 'waiting-for-updated-watch';
          send('evaluate', {
            frameId: topFrameId,
            expression: 'snapshot.point.x + snapshot.point.y + extra',
            context: 'watch',
          });
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-updated-watch') {
          assert.equal(message.success, true);
          assert.equal(responseBody(message, 'evaluate').result, '45');
          state = 'waiting-for-invalid-watch';
          send('evaluate', { frameId: topFrameId, expression: 'missing_local + 1', context: 'watch' });
        } else if (message.type === 'response' && message.command === 'evaluate' && state === 'waiting-for-invalid-watch') {
          assert.equal(message.success, false);
          assert.match(String(message.message), /error:|undeclared|not found/i);
          state = 'waiting-for-invalid-write';
          send('setVariable', { variablesReference: pointReference, name: 'x + 1', value: '7' });
        } else if (message.type === 'response' && message.command === 'setVariable' && state === 'waiting-for-invalid-write') {
          assert.equal(message.success, false);
          assert.match(String(message.message), /error:|assignable|lvalue|not writable/i);
          state = 'waiting-for-disconnect';
          send('disconnect');
        } else if (message.type === 'response' && message.command === 'disconnect' && state === 'waiting-for-disconnect') {
          assert.equal(nestedExpressionChecked, true);
          assert.equal(changed, true);
          finish();
        }
      },
    });

    assert.ok(messages.some((message) => message.type === 'event' && message.event === 'stopped'));
  });

  test('reports a native signal through stopped and exceptionInfo', async () => {
    const repositoryRoot = path.resolve(__dirname, '../../../../..');
    const helperSource = path.join(repositoryRoot, 'src/tests/debug/signal_helper.c');
    const helperDirectory = mkdtempSync(path.join(os.tmpdir(), 'vitte-dap-signal-'));
    const helperProgram = path.join(helperDirectory, 'signal-helper');
    const compiler = process.env.CC?.trim() ?? '';
    execFileSync(compiler.length > 0 ? compiler : 'cc', [helperSource, '-o', helperProgram], {
      cwd: repositoryRoot,
      stdio: 'pipe',
    });
    let exceptionReported = false;
    let stopped = false;
    try {
      const messages = await runNativeDapScenario({
        program: helperProgram,
        onMessage: (message, send, finish) => {
          if (message.type === 'event' && message.event === 'stopped' && !stopped) {
            stopped = true;
            assert.equal(message.body?.reason, 'exception');
            send('exceptionInfo', { threadId: 1 });
          } else if (message.type === 'response' && message.command === 'exceptionInfo') {
            const body = responseBody(message, 'exceptionInfo');
            assert.match(String(body.description), /signal|SIGABRT/i);
            exceptionReported = true;
            send('disconnect');
          } else if (message.type === 'response' && message.command === 'disconnect') {
            assert.equal(exceptionReported, true);
            finish();
          }
        },
      });

      assert.equal(stopped, true);
      assert.ok(messages.some((message) => message.type === 'response' && message.command === 'exceptionInfo'));
    } finally {
      rmSync(helperDirectory, { recursive: true, force: true });
    }
  });
});
