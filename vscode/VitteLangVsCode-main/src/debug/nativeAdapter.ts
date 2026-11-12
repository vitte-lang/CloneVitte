/*
 * Native Vitte DAP adapter.
 *
 * The native backend is an LLDB front-end.  Vitte is compiled with DWARF and
 * source line directives, then LLDB provides the process, frame, scope and
 * expression model exposed through DAP.  No compiler-injected probe protocol
 * is used by this adapter.
 */

import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import * as os from 'node:os';
import * as path from 'node:path';

type JsonObject = Record<string, unknown>;

interface DapRequest {
  seq: number;
  type: 'request';
  command: string;
  arguments?: JsonObject;
}

interface DapResponse {
  seq: number;
  type: 'response';
  request_seq: number;
  success: boolean;
  command: string;
  message?: string;
  body?: JsonObject;
}

interface LaunchArguments extends JsonObject {
  program?: unknown;
  args?: unknown;
  cwd?: unknown;
  env?: unknown;
  stopOnEntry?: unknown;
}

interface SourceDescriptor {
  name: string;
  path: string;
}

interface FrameInfo {
  id: number;
  lldbIndex: number;
  name: string;
  line: number;
  column: number;
  sourcePath?: string;
}

type ParsedFrameInfo = Omit<FrameInfo, 'id'>;

interface BreakpointSpec {
  line: number;
  condition?: string;
  hitCondition?: string;
  logMessage?: string;
  lldbId?: number;
}

interface VariableHandle {
  frameIndex: number;
  expression: string;
  type?: string;
  pointer?: boolean;
}

interface ParsedValue {
  indent: number;
  type: string;
  name: string;
  value: string;
  declarationLine?: number;
}

const SOURCE_REFERENCE = 1;
const MAIN_THREAD_ID = 1;
const FRAME_ID_BASE = 1000;
const VARIABLES_HANDLE_BASE = 10000;
const LLDB_PROMPT = '__VITTE_LLDB_PROMPT__';
const LLDB_PROMPT_MARKER = `\r\n${LLDB_PROMPT}`;

let sequence = 1;
let inputBuffer = Buffer.alloc(0);
let lldb: LldbSession | undefined;
let lldbCwd = process.cwd();
let source: SourceDescriptor | undefined;
let executablePath: string | undefined;
let temporaryDirectory: string | undefined;
let debuggeePid: number | undefined;
let debuggeeStarted = false;
let debuggeeExited = false;
let stopOnEntry = false;
let frames: FrameInfo[] = [];
let nextFrameId = FRAME_ID_BASE;
let currentStopReason = '';
let breakpointSpecs: BreakpointSpec[] = [];
const variableHandles = new Map<number, VariableHandle>();
let nextVariableHandle = VARIABLES_HANDLE_BASE;
let exitEventSent = false;
let frameCommandQueue: Promise<void> = Promise.resolve();

function stringValue(value: unknown): string | undefined {
  return typeof value === 'string' && value.trim().length > 0 ? value.trim() : undefined;
}

function stringArray(value: unknown): string[] {
  if (!Array.isArray(value)) return [];
  return value.filter((item): item is string => typeof item === 'string');
}

function environmentValue(value: unknown): Record<string, string> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return {};
  const result: Record<string, string> = {};
  for (const [key, item] of Object.entries(value)) {
    if (typeof item === 'string') result[key] = item;
  }
  return result;
}

function send(message: JsonObject): void {
  const payload = JSON.stringify(message);
  process.stdout.write(`Content-Length: ${Buffer.byteLength(payload, 'utf8')}\r\n\r\n${payload}`);
}

function respond(request: DapRequest, success: boolean, body?: JsonObject, message?: string): void {
  const response: DapResponse = {
    seq: sequence++,
    type: 'response',
    request_seq: request.seq,
    success,
    command: request.command,
  };
  if (body) response.body = body;
  if (message) response.message = message;
  send(response as unknown as JsonObject);
}

function event(eventName: string, body?: JsonObject): void {
  const message: JsonObject = { seq: sequence++, type: 'event', event: eventName };
  if (body) message.body = body;
  send(message);
}

function output(category: 'stdout' | 'stderr' | 'console', text: string): void {
  if (text) event('output', { category, output: text });
}

function requestObject(request: DapRequest): JsonObject {
  return request.arguments ?? {};
}

function launchObject(request: DapRequest): LaunchArguments {
  return requestObject(request) as LaunchArguments;
}

function quoteLldb(value: string): string {
  return `"${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n')}` + '"';
}

function isVitteSource(program: string): boolean {
  return /\.(vit|vitl|vitte)$/i.test(program);
}

function compilerPath(): string {
  return stringValue(process.env.VITTE_COMPILER) ?? 'vitte';
}

function lldbPath(): string {
  return stringValue(process.env.VITTE_LLDB) ?? 'lldb';
}

function pathForProgram(program: string, cwd: string): string {
  return path.isAbsolute(program) ? program : path.resolve(cwd, program);
}

function spawnEnvironment(argumentsObject: LaunchArguments): NodeJS.ProcessEnv {
  return { ...process.env, ...environmentValue(argumentsObject.env) };
}

function sourceForPath(candidate: string | undefined): SourceDescriptor | undefined {
  if (!candidate) return undefined;
  const resolved = path.isAbsolute(candidate) ? candidate : path.resolve(lldbCwd, candidate);
  if (source && (resolved === source.path || path.basename(resolved) === source.name || path.basename(candidate) === source.name)) {
    return source;
  }
  if (!isVitteSource(resolved)) return undefined;
  return { name: path.basename(resolved), path: resolved };
}

function sourceBody(frame: FrameInfo): JsonObject | undefined {
  const frameSource = sourceForPath(frame.sourcePath);
  if (!frameSource) return undefined;
  return { name: frameSource.name, path: frameSource.path, sourceReference: SOURCE_REFERENCE };
}

function normalizeFrameLocation(location: string | undefined): { sourcePath?: string; line: number; column: number } {
  if (!location) return { line: 1, column: 1 };
  const match = /^(.*?):(\d+)(?::(\d+))?$/.exec(location.trim());
  if (!match) return { line: 1, column: 1 };
  const line = Number(match[2]);
  const column = Number(match[3] ?? '1');
  return {
    ...(match[1] ? { sourcePath: match[1] } : {}),
    line: Number.isSafeInteger(line) && line > 0 ? line : 1,
    column: Number.isSafeInteger(column) && column > 0 ? column : 1,
  };
}

function parseFrames(text: string): ParsedFrameInfo[] {
  const parsed: ParsedFrameInfo[] = [];
  for (const line of text.split(/\r?\n/)) {
    const match = /^\s*(?:\*\s+)?frame #(\d+):\s*(.*)$/.exec(line);
    if (!match) continue;
    const lldbIndex = Number(match[1]);
    if (!Number.isSafeInteger(lldbIndex)) continue;
    const tick = (match[2] ?? '').indexOf('`');
    if (tick < 0) continue;
    const tail = (match[2] ?? '').slice(tick + 1);
    const at = tail.indexOf(' at ');
    const rawName = (at >= 0 ? tail.slice(0, at) : tail).trim();
    // LLDB appends the current argument list to source-level function names.
    // DAP clients use the stable callable name for navigation and display.
    const name = rawName.replace(/\([^()]*\)$/, '').trim();
    const location = normalizeFrameLocation(at >= 0 ? tail.slice(at + 4) : undefined);
    parsed.push({
      lldbIndex,
      name: name || `frame-${lldbIndex}`,
      line: location.line,
      column: location.column,
      ...(location.sourcePath ? { sourcePath: location.sourcePath } : {}),
    });
  }
  const unique = new Map<number, ParsedFrameInfo>();
  for (const frame of parsed) unique.set(frame.lldbIndex, frame);
  return Array.from(unique.values()).sort((left, right) => left.lldbIndex - right.lldbIndex);
}

function stopReason(text: string): string {
  return /stop reason = ([^\r\n]+)/.exec(text)?.[1]?.trim() ?? '';
}

function dapStopReason(reason: string): string {
  const lower = reason.toLowerCase();
  if (lower.includes('breakpoint')) return 'breakpoint';
  if (lower.includes('exception') || lower.includes('signal') || lower.includes('exc_')) return 'exception';
  if (lower.includes('step') || lower.includes('plan complete')) return 'step';
  if (lower.includes('entry')) return 'entry';
  return 'pause';
}

function frameIndexFromId(id: unknown): number {
  const numeric = Number(id);
  if (!Number.isSafeInteger(numeric)) return -1;
  return frames.findIndex((frame) => frame.id === numeric);
}

function allocateVariableHandle(handle: VariableHandle): number {
  const id = nextVariableHandle++;
  variableHandles.set(id, handle);
  return id;
}

function resetVariableHandles(): void {
  variableHandles.clear();
}

function lldbError(text: string): string | undefined {
  // LLDB's expression diagnostics may start with a source-code gutter such as
  // "╰─ error:". A literal value containing "error:" is not a diagnostic.
  for (const line of text.split(/\r?\n/)) {
    const match = /^[^\p{L}\p{N}]*?(error:\s*.*)$/iu.exec(line.trim());
    if (match) return match[1];
  }
  return undefined;
}

function singleLineExpression(value: string): string {
  if (/[\r\n]/.test(value)) throw new Error('LLDB expressions must be a single line.');
  return value;
}

function looksExpandable(type: string, value: string): boolean {
  if (type.includes('*') || value.includes('{') || /^\(.*\)$/.test(value) || value.startsWith('0x')) return true;
  return !/^(?:bool|char|signed char|unsigned char|short|unsigned short|int|unsigned int|long|unsigned long|long long|unsigned long long|float|double|long double|int\d+_t|uint\d+_t)$/i.test(type);
}

function parseValueLines(text: string, includeIndented = false): ParsedValue[] {
  const values: ParsedValue[] = [];
  for (const line of text.split(/\r?\n/)) {
    const declaration = /^.*:(\d+):\s+(?=\()/.exec(line);
    const payload = declaration ? line.slice(declaration[0].length) : line;
    const match = /^(\s*)\(([^)]+)\)\s+([^=]+?)\s*=\s*(.*)$/.exec(payload);
    if (!match || (!includeIndented && match[1]?.length !== 0)) continue;
    const name = match[3]?.trim();
    if (!name || name.includes(' ')) continue;
    const declarationLine = declaration ? Number(declaration[1]) : undefined;
    const value: ParsedValue = {
      indent: match[1]?.length ?? 0,
      type: match[2]?.trim() ?? '',
      name,
      value: match[4] ?? '',
    };
    if (declarationLine !== undefined && Number.isSafeInteger(declarationLine)) value.declarationLine = declarationLine;
    values.push(value);
  }
  return values;
}

function childExpression(parent: string, parentPointer: boolean, child: string): string {
  if (child.startsWith('[')) return `${parent}${child}`;
  if (child.startsWith('*')) return `*(${parent})`;
  return `(${parent})${parentPointer ? '->' : '.'}${child}`;
}

function dapVariable(
  item: { type: string; name: string; value: string },
  frameIndex: number,
  expression: string,
): JsonObject {
  const expandable = looksExpandable(item.type, item.value);
  const variablesReference = expandable
    ? allocateVariableHandle({
      frameIndex,
      expression,
      type: item.type,
      pointer: item.type.includes('*'),
    })
    : 0;
  return {
    name: item.name,
    value: item.value || '<unavailable>',
    type: item.type,
    variablesReference,
  };
}

async function compileProgram(program: string, cwd: string, env: NodeJS.ProcessEnv): Promise<string> {
  temporaryDirectory = await mkdtemp(path.join(os.tmpdir(), 'vitte-dap-'));
  const target = path.join(temporaryDirectory, 'program');
  await new Promise<void>((resolve, reject) => {
    const compiler = spawn(
      compilerPath(),
      ['compile', '--debug-info', '--line-directives', program, '-o', target],
      { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] },
    );
    compiler.stdout.on('data', (chunk: Buffer | string) => output('stdout', String(chunk)));
    compiler.stderr.on('data', (chunk: Buffer | string) => output('stderr', String(chunk)));
    compiler.once('error', (error) => reject(new Error(`Vitte compiler could not be started: ${error.message}`)));
    compiler.once('exit', (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(`Vitte compilation failed${signal ? ` (${signal})` : ` (exit ${code ?? 'unknown'})`}.`));
    });
  });
  return target;
}

class LldbSession {
  readonly process: ChildProcessWithoutNullStreams;
  private outputBuffer = '';
  private queue: Promise<void> = Promise.resolve();

  constructor(cwd: string, env: NodeJS.ProcessEnv) {
    const escapedLldbPath = lldbPath().replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    const command = process.platform === 'win32' ? lldbPath() : 'expect';
    const args = process.platform === 'win32'
      ? ['-x', '-Q', '--no-use-colors']
      : ['-c', `set timeout -1; spawn -noecho "${escapedLldbPath}" -x -Q --no-use-colors; interact`];
    this.process = spawn(command, args, {
      cwd,
      env,
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.process.setMaxListeners(0);
    this.process.stdout.on('data', (chunk: Buffer | string) => { this.outputBuffer += String(chunk); });
    this.process.stderr.on('data', (chunk: Buffer | string) => output('stderr', String(chunk)));
  }

  private waitFor(marker: string): Promise<string> {
    const existing = this.outputBuffer.indexOf(marker);
    if (existing >= 0) {
      const result = this.outputBuffer.slice(0, existing);
      this.outputBuffer = this.outputBuffer.slice(existing + marker.length);
      return Promise.resolve(result);
    }
    return new Promise<string>((resolve, reject) => {
      const timer = setInterval(() => {
        const index = this.outputBuffer.indexOf(marker);
        if (index < 0) return;
        clearInterval(timer);
        const result = this.outputBuffer.slice(0, index);
        this.outputBuffer = this.outputBuffer.slice(index + marker.length);
        resolve(result);
      }, 5);
      this.process.once('error', (error) => {
        clearInterval(timer);
        reject(error);
      });
      this.process.once('exit', (code, signal) => {
        if (code === 0) return;
        clearInterval(timer);
        reject(new Error(`LLDB exited${signal ? ` by ${signal}` : ` with code ${code ?? 'unknown'}`}.`));
      });
    });
  }

  async start(): Promise<void> {
    await this.waitFor('(lldb) ');
    await this.raw(`settings set prompt ${quoteLldb(LLDB_PROMPT)}`);
  }

  private async raw(command: string): Promise<string> {
    // The expect/PTY bridge echoes commands.  Waiting for the marker alone
    // would match the marker text inside `settings set prompt "..."`.
    const result = this.waitFor(LLDB_PROMPT_MARKER);
    this.process.stdin.write(`${command}\n`);
    return result;
  }

  command(command: string): Promise<string> {
    const task = this.queue.then(() => this.raw(command));
    this.queue = task.then(() => undefined, () => undefined);
    return task;
  }

  async dispose(): Promise<void> {
    if (this.process.exitCode !== null) return;
    this.process.kill('SIGTERM');
    await new Promise<void>((resolve) => setTimeout(resolve, 50));
    if (this.process.exitCode === null) this.process.kill('SIGKILL');
  }
}

async function refreshFrames(text?: string): Promise<void> {
  if (!lldb) return;
  const backtrace = await lldb.command('bt');
  const parsed = parseFrames(`${text ?? ''}\n${backtrace}`);
  frames = (parsed.length > 0 ? parsed : parseFrames(backtrace)).map((frame) => ({
    ...frame,
    id: nextFrameId++,
  }));
}

function frameForRequest(request: DapRequest): number {
  return frameIndexFromId(requestObject(request).frameId);
}

function commandInFrame(frameIndex: number, command: string): Promise<string> {
  const session = lldb;
  const frame = frames[frameIndex];
  if (!session) return Promise.reject(new Error('LLDB is not running.'));
  if (!frame) return Promise.reject(new Error('The selected stack frame is no longer available.'));
  // A DAP client may issue watches for several frames at once. Keep each
  // select+command pair atomic so a parallel watch cannot switch LLDB's frame.
  const task = frameCommandQueue.then(async () => {
    if (lldb !== session || frames[frameIndex]?.id !== frame.id) {
      throw new Error('The selected stack frame is no longer available.');
    }
    const selected = await session.command(`frame select ${frame.lldbIndex}`);
    const error = lldbError(selected);
    if (error) throw new Error(error);
    return session.command(command);
  });
  frameCommandQueue = task.then(() => undefined, () => undefined);
  return task;
}

async function frameVariables(frameIndex: number, expression?: string): Promise<ParsedValue[]> {
  const suffix = expression ? ` -- ${expression}` : '';
  const values = await commandInFrame(frameIndex, expression ? `frame variable -T -c -A -P 2${suffix}` : 'frame variable -T -c');
  const error = lldbError(values);
  if (error) throw new Error(error);
  return parseValueLines(values, Boolean(expression));
}

async function evaluateInFrame(frameIndex: number, expression: string): Promise<{ result: string; type?: string; variablesReference: number }> {
  if (/^[A-Za-z_][A-Za-z_0-9]*$/.test(expression)) {
    const bindings = (await frameVariables(frameIndex)).filter((item) => item.name === expression);
    const currentLine = frames[frameIndex]?.line ?? Number.MAX_SAFE_INTEGER;
    if (bindings.length > 0 && bindings.every((item) => item.declarationLine !== undefined && item.declarationLine > currentLine)) {
      throw new Error(`The variable "${expression}" is not yet in scope.`);
    }
  }
  const text = await commandInFrame(frameIndex, `expression -- ${singleLineExpression(expression)}`);
  const error = lldbError(text);
  if (error) {
    // LLDB sometimes cannot infer a Vitte local's expression type in a caller
    // frame even though its DWARF location and value are available to `frame
    // variable`. Preserve simple watches in that case.
    if (/unknown type|type is not known/i.test(error) && /^[A-Za-z_][A-Za-z_0-9]*$/.test(expression)) {
      const locals = await frameVariables(frameIndex, expression);
      const local = locals.find((item) => item.name === expression);
      if (local) {
        const ref = looksExpandable(local.type, local.value)
          ? allocateVariableHandle({ frameIndex, expression, type: local.type, pointer: local.type.includes('*') })
          : 0;
        return { result: local.value || '<unavailable>', type: local.type, variablesReference: ref };
      }
    }
    throw new Error(error);
  }
  const values = parseValueLines(text, true);
  const value = values.length > 0 ? values[values.length - 1] : undefined;
  if (value) {
    const ref = looksExpandable(value.type, value.value)
      ? allocateVariableHandle({ frameIndex, expression, type: value.type, pointer: value.type.includes('*') })
      : 0;
    return { result: value.value || '<unavailable>', type: value.type, variablesReference: ref };
  }
  const lines = text.split(/\r?\n/).map((item) => item.trim()).filter(Boolean);
  throw new Error(`LLDB could not evaluate "${expression}": ${lines.join(' | ') || '<no output>'}`);
}

async function interpolateLogMessage(message: string, frameIndex: number): Promise<string> {
  const parts: string[] = [];
  let cursor = 0;
  const pattern = /\{([^{}]+)\}/g;
  for (;;) {
    const match = pattern.exec(message);
    if (!match) break;
    parts.push(message.slice(cursor, match.index));
    try {
      parts.push((await evaluateInFrame(frameIndex, match[1] ?? '')).result);
    } catch {
      parts.push(`<error evaluating ${match[1] ?? 'expression'}>`);
    }
    cursor = match.index + match[0].length;
  }
  parts.push(message.slice(cursor));
  return parts.join('');
}

function processExited(text: string): boolean {
  return /Process \d+ (?:exited|detached)/i.test(text) || /exited with status/i.test(text);
}

async function handleStop(text: string, forcedReason?: string): Promise<void> {
  if (!lldb || debuggeeExited) return;
  currentStopReason = forcedReason ?? stopReason(text);
  if (processExited(text)) {
    debuggeeExited = true;
    if (!exitEventSent) {
      exitEventSent = true;
      event('exited', { exitCode: 0 });
      event('terminated');
    }
    return;
  }
  await refreshFrames(text);
  if (frames.length === 0) return;
  const current = frames[0];
  if (!current) return;
  // LLDB's lexical variable view is valid only for the current stop.  Dropping
  // handles here prevents a DAP client from reading a dead shadowed binding
  // after stepping out of its scope or continuing to another stop.
  resetVariableHandles();
  const reason = dapStopReason(currentStopReason);
  const breakpointId = reason === 'breakpoint'
    ? Number(/breakpoint\s+(\d+)/i.exec(currentStopReason)?.[1])
    : NaN;
  const breakpoint = reason === 'breakpoint'
    ? breakpointSpecs.find((item) => item.lldbId === breakpointId) ??
      breakpointSpecs.find((item) => item.line === current.line)
    : undefined;
  if (breakpoint?.logMessage) {
    const message = await interpolateLogMessage(breakpoint.logMessage, 0);
    output('console', `${message}\n`);
    await continueLldb(false);
    return;
  }
  event('stopped', {
    reason,
    threadId: MAIN_THREAD_ID,
    allThreadsStopped: true,
    ...(reason === 'exception' ? { description: currentStopReason } : {}),
  });
}

async function continueLldb(emitContinued: boolean): Promise<void> {
  if (!lldb || debuggeeExited) return;
  if (emitContinued) event('continued', { threadId: MAIN_THREAD_ID, allThreadsContinued: true });
  const text = await lldb.command('process continue');
  await handleStop(text);
}

async function installBreakpoints(requested: unknown[], requestedPath: string | undefined): Promise<JsonObject[]> {
  if (!lldb) return requested.map(() => ({ verified: false, message: 'LLDB target is not initialized.' }));
  await lldb.command('breakpoint delete --force');
  breakpointSpecs = [];
  const matchesSource = Boolean(source && requestedPath && path.resolve(requestedPath) === source.path);
  const response: JsonObject[] = [];
  for (const item of requested) {
    const object = item && typeof item === 'object' ? item as JsonObject : {};
    const line = Number(object.line);
    const condition = stringValue(object.condition);
    const hitCondition = stringValue(object.hitCondition);
    const logMessage = stringValue(object.logMessage);
    const spec: BreakpointSpec = {
      line,
      ...(condition ? { condition } : {}),
      ...(hitCondition ? { hitCondition } : {}),
      ...(logMessage ? { logMessage } : {}),
    };
    if (!matchesSource || !Number.isSafeInteger(line) || line < 1) {
      response.push({ verified: false, message: 'The source is not controlled by the native Vitte runtime.' });
      continue;
    }
    // The C backend emits the Vitte basename in #line directives.  LLDB can
    // still map it to the absolute DAP source through the source descriptor.
    const lldbSourceName = source?.name ?? path.basename(requestedPath ?? '');
    const outputText = await lldb.command(`breakpoint set --file ${quoteLldb(lldbSourceName)} --line ${line}`);
    const idMatch = /Breakpoint (\d+)/.exec(outputText);
    const id = idMatch ? Number(idMatch[1]) : undefined;
    if (!id) {
      response.push({ verified: false, line, message: 'LLDB did not resolve this source line.' });
      continue;
    }
    spec.lldbId = id;
    if (spec.condition) await lldb.command(`breakpoint modify ${id} --condition ${quoteLldb(spec.condition)}`);
    const hitCount = Number(spec.hitCondition);
    if (Number.isSafeInteger(hitCount) && hitCount > 1) await lldb.command(`breakpoint modify ${id} --ignore-count ${hitCount - 1}`);
    breakpointSpecs.push(spec);
    response.push({ verified: true, line, id });
  }
  return response;
}

async function compileAndCreateTarget(program: string, cwd: string, env: NodeJS.ProcessEnv): Promise<void> {
  executablePath = source ? await compileProgram(program, cwd, env) : program;
  lldb = new LldbSession(cwd, env);
  await lldb.start();
  const targetOutput = await lldb.command(`target create ${quoteLldb(executablePath)}`);
  if (/error:/i.test(targetOutput)) throw new Error(targetOutput.trim());
  // Keep fatal runtime signals visible to DAP instead of letting LLDB pass
  // them straight through to the debuggee.  This also makes exceptionInfo
  // deterministic for native Vitte programs that fault in generated C.
  await lldb.command(
    'process handle -s true -n true -p true SIGABRT SIGBUS SIGFPE SIGILL SIGSEGV SIGTRAP',
  );
  await lldb.command(`settings set -- target.process.cwd ${quoteLldb(cwd)}`);
  const args = stringArray((launchObject(currentLaunchRequest!)).args);
  if (args.length > 0) {
    await lldb.command(`settings set -- target.run-args ${args.map(quoteLldb).join(' ')}`);
  } else {
    await lldb.command('settings clear target.run-args');
  }
}

let currentLaunchRequest: DapRequest | undefined;

async function startDebuggee(): Promise<void> {
  if (!lldb || debuggeeStarted) return;
  debuggeeStarted = true;
  let entryBreakpointId: number | undefined;
  if (stopOnEntry && source) {
    // `--stop-at-entry` stops in dyld/crt on macOS.  A temporary breakpoint on
    // the generated Vitte trampoline gives DAP the first real Vitte frame.
    const entryOutput = await lldb.command('breakpoint set --name vitte_entry_main');
    const entryId = /Breakpoint (\d+)/.exec(entryOutput)?.[1];
    entryBreakpointId = entryId ? Number(entryId) : undefined;
  }
  const text = await lldb.command('process launch --stop-at-entry');
  const pid = /Process (\d+) launched/.exec(text)?.[1];
  debuggeePid = pid ? Number(pid) : undefined;
  if (stopOnEntry) {
    const entryText = source && entryBreakpointId
      ? await lldb.command('process continue')
      : text;
    await handleStop(entryText, 'entry');
    if (entryBreakpointId) await lldb.command(`breakpoint delete ${entryBreakpointId}`);
  } else if (!debuggeeExited) {
    await continueLldb(false);
  }
}

async function cleanup(): Promise<void> {
  if (lldb) await lldb.dispose();
  lldb = undefined;
  const directory = temporaryDirectory;
  temporaryDirectory = undefined;
  executablePath = undefined;
  source = undefined;
  frames = [];
  breakpointSpecs = [];
  resetVariableHandles();
  if (directory) await rm(directory, { recursive: true, force: true });
}

async function launch(request: DapRequest): Promise<void> {
  const argumentsObject = launchObject(request);
  const programValue = stringValue(argumentsObject.program);
  if (!programValue) {
    respond(request, false, undefined, 'The native Vitte adapter requires a source file or executable in "program".');
    return;
  }
  const cwd = stringValue(argumentsObject.cwd) ?? process.cwd();
  const program = pathForProgram(programValue, cwd);
  const env = spawnEnvironment(argumentsObject);
  lldbCwd = cwd;
  source = isVitteSource(program) ? { name: path.basename(program), path: program } : undefined;
  stopOnEntry = argumentsObject.stopOnEntry === true;
  currentLaunchRequest = request;
  debuggeeStarted = false;
  debuggeeExited = false;
  exitEventSent = false;
  try {
    await compileAndCreateTarget(program, cwd, env);
    respond(request, true);
    event('process', {
      name: path.basename(executablePath ?? program),
      ...(debuggeePid ? { systemProcessId: debuggeePid } : {}),
      startMethod: 'launch',
    });
    event('initialized');
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    output('stderr', `${message}\n`);
    respond(request, false, undefined, message);
    await cleanup();
  }
}

async function handle(request: DapRequest): Promise<void> {
  switch (request.command) {
    case 'initialize':
      respond(request, true, {
        supportsConfigurationDoneRequest: true,
        supportsTerminateRequest: true,
        supportsRestartRequest: false,
        supportsBreakpoints: true,
        supportsSourceRequest: true,
        supportsSteppingGranularity: true,
        supportsFunctionBreakpoints: false,
        supportsConditionalBreakpoints: true,
        supportsInstructionBreakpoints: false,
        supportsEvaluateForHovers: true,
        supportsSetVariable: true,
        supportsLogPoints: true,
        supportsExceptionInfoRequest: true,
      });
      event('output', { category: 'console', output: 'Vitte native DAP backend active (LLDB/DWARF).\n' });
      return;

    case 'launch':
      await launch(request);
      return;

    case 'configurationDone':
      respond(request, true);
      await startDebuggee();
      return;

    case 'setBreakpoints': {
      const sourceObject = requestObject(request).source as JsonObject | undefined;
      const requested = Array.isArray(requestObject(request).breakpoints) ? requestObject(request).breakpoints as unknown[] : [];
      const requestedPath = stringValue(sourceObject?.path);
      const breakpoints = await installBreakpoints(requested, requestedPath);
      respond(request, true, { breakpoints });
      return;
    }

    case 'threads':
      respond(request, true, { threads: lldb && !debuggeeExited ? [{ id: MAIN_THREAD_ID, name: 'main' }] : [] });
      return;

    case 'stackTrace': {
      if (lldb && !debuggeeExited && frames.length === 0) await refreshFrames();
      const args = requestObject(request);
      const startFrame = Math.max(0, Number(args.startFrame) || 0);
      const levels = Math.max(0, Number(args.levels) || frames.length);
      const selected = frames.slice(startFrame, startFrame + levels);
      respond(request, true, {
        stackFrames: selected.map((frame) => ({
          id: frame.id,
          name: frame.name,
          line: frame.line,
          column: frame.column,
          ...(sourceBody(frame) ? { source: sourceBody(frame) } : {}),
        })),
        totalFrames: frames.length,
      });
      return;
    }

    case 'scopes': {
      const frameIndex = frameForRequest(request);
      if (frameIndex < 0) {
        respond(request, false, undefined, 'The selected stack frame is no longer available.');
        return;
      }
      const handle = allocateVariableHandle({ frameIndex, expression: '' });
      respond(request, true, { scopes: [{ name: 'Locals', variablesReference: handle, expensive: false }] });
      return;
    }

    case 'variables': {
      const reference = Number(requestObject(request).variablesReference);
      const handle = variableHandles.get(reference);
      if (!handle) {
        respond(request, true, { variables: [] });
        return;
      }
      const values = await frameVariables(handle.frameIndex, handle.expression || undefined);
      const rootIndent = values[0]?.indent ?? 0;
      const childIndent = handle.expression
        ? values
          .filter((item) => item.indent > rootIndent)
          .reduce<number | undefined>((minimum, item) => minimum === undefined ? item.indent : Math.min(minimum, item.indent), undefined)
        : rootIndent;
      let children = handle.expression && childIndent !== undefined
        ? values.filter((item) => item.indent === childIndent)
        : values.filter((item) => item.indent === rootIndent);
      if (!handle.expression) {
        // LLDB can report all lexical bindings known to the current function,
        // including a later declaration and both sides of a shadowing name.
        // DWARF declaration lines let us model the binding visible at the
        // current source location instead of leaking future/dead locals.
        const currentLine = frames[handle.frameIndex]?.line ?? Number.MAX_SAFE_INTEGER;
        const visible = children.filter((item) => item.declarationLine === undefined || item.declarationLine <= currentLine);
        const byName = new Map<string, ParsedValue>();
        for (const item of visible) byName.set(item.name, item);
        children = Array.from(byName.values());
      }
      respond(request, true, {
        variables: children.map((item) => {
          const expression = handle.expression
            ? childExpression(handle.expression, handle.pointer === true, item.name)
            : item.name;
          return dapVariable(item, handle.frameIndex, expression);
        }),
      });
      return;
    }

    case 'evaluate': {
      const expression = stringValue(requestObject(request).expression);
      if (!expression) {
        respond(request, false, undefined, 'An expression is required.');
        return;
      }
      try {
        const frameIndex = requestObject(request).frameId === undefined ? 0 : frameForRequest(request);
        const value = await evaluateInFrame(frameIndex, expression);
        respond(request, true, value);
      } catch (error) {
        respond(request, false, undefined, error instanceof Error ? error.message : String(error));
      }
      return;
    }

    case 'setVariable': {
      const reference = Number(requestObject(request).variablesReference);
      const handle = variableHandles.get(reference);
      const name = stringValue(requestObject(request).name);
      const value = stringValue(requestObject(request).value);
      if (!lldb || !handle || !name || !/^(?:[A-Za-z_][A-Za-z_0-9]*|\[\d+\]|\*)$/.test(name) || value === undefined) {
        respond(request, false, undefined, 'The requested variable is not writable.');
        return;
      }
      const expression = handle.expression
        ? childExpression(handle.expression, handle.pointer === true, name)
        : name;
      try {
        if (!handle.expression) {
          const currentLine = frames[handle.frameIndex]?.line ?? Number.MAX_SAFE_INTEGER;
          const locals = await frameVariables(handle.frameIndex);
          const visible = locals.some((item) => item.name === name && (item.declarationLine === undefined || item.declarationLine <= currentLine));
          if (!visible) throw new Error(`The variable "${name}" is not writable in this scope.`);
        }
        const text = await commandInFrame(handle.frameIndex, `expression -- ${singleLineExpression(expression)} = ${singleLineExpression(value)}`);
        const error = lldbError(text);
        if (error) throw new Error(error);
        const updated = await evaluateInFrame(handle.frameIndex, expression);
        respond(request, true, {
          value: updated.result,
          ...(updated.type ? { type: updated.type } : {}),
          variablesReference: updated.variablesReference,
        });
      } catch (error) {
        respond(request, false, undefined, error instanceof Error ? error.message : String(error));
      }
      return;
    }

    case 'source': {
      const requestedSource = requestObject(request).source as JsonObject | undefined;
      const requestedPath = stringValue(requestedSource?.path) ?? source?.path;
      if (!requestedPath) {
        respond(request, false, undefined, 'Source is not available.');
        return;
      }
      try {
        const content = await readFile(requestedPath, 'utf8');
        respond(request, true, { content, mimeType: 'text/x-vitte' });
      } catch (error) {
        respond(request, false, undefined, error instanceof Error ? error.message : String(error));
      }
      return;
    }

    case 'continue':
      respond(request, true, { allThreadsContinued: true });
      await continueLldb(true);
      return;

    case 'pause':
      if (!lldb || debuggeeExited) {
        respond(request, false, undefined, 'The Vitte debuggee is not running.');
        return;
      }
      respond(request, true);
      await handleStop(await lldb.command('process interrupt'), 'pause');
      return;

    case 'next':
    case 'stepIn':
    case 'stepOut': {
      if (!lldb || debuggeeExited) {
        respond(request, false, undefined, 'The Vitte debuggee is not running.');
        return;
      }
      const command = request.command === 'next' ? 'thread step-over' : request.command === 'stepIn' ? 'thread step-in' : 'thread step-out';
      respond(request, true);
      event('continued', { threadId: MAIN_THREAD_ID, allThreadsContinued: true });
      await handleStop(await lldb.command(command));
      return;
    }

    case 'exceptionInfo':
      respond(request, true, {
        exceptionId: currentStopReason || 'native-stop',
        description: currentStopReason || 'Native Vitte process stop',
        breakMode: 'always',
      });
      return;

    case 'terminate':
    case 'disconnect':
      respond(request, true);
      await cleanup();
      if (request.command === 'disconnect') setTimeout(() => process.exit(0), 0);
      return;

    default:
      respond(request, false, undefined, `Unsupported native Vitte DAP request: ${request.command}`);
  }
}

function parseMessages(): void {
  for (;;) {
    const separator = inputBuffer.indexOf('\r\n\r\n');
    if (separator < 0) return;
    const header = inputBuffer.subarray(0, separator).toString('ascii');
    const match = /(?:^|\r\n)Content-Length:\s*(\d+)/i.exec(header);
    if (!match) {
      inputBuffer = inputBuffer.subarray(separator + 4);
      continue;
    }
    const length = Number(match[1]);
    const bodyStart = separator + 4;
    if (!Number.isSafeInteger(length) || inputBuffer.length < bodyStart + length) return;
    const body = inputBuffer.subarray(bodyStart, bodyStart + length).toString('utf8');
    inputBuffer = inputBuffer.subarray(bodyStart + length);
    try {
      const request = JSON.parse(body) as DapRequest;
      if (request.type === 'request' && typeof request.command === 'string') {
        void handle(request).catch((error: unknown) => {
          respond(request, false, undefined, error instanceof Error ? error.message : String(error));
        });
      }
    } catch (error) {
      output('stderr', `Invalid DAP JSON: ${error instanceof Error ? error.message : String(error)}\n`);
    }
  }
}

process.stdin.on('data', (chunk: Buffer | string) => {
  inputBuffer = Buffer.concat([inputBuffer, Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)]);
  parseMessages();
});
process.stdin.on('end', () => { void cleanup(); });
process.on('SIGTERM', () => { void cleanup().finally(() => { process.exitCode = 0; }); });
