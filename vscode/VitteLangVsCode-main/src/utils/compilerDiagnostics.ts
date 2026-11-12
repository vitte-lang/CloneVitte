import * as path from 'node:path';

export interface CompilerDiagnostic {
  severity?: string | undefined;
  code?: string | number | undefined;
  message?: string | undefined;
  start: number;
  end: number;
}

function record(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {};
}

/** Native diagnostic spans are UTF-8 byte offsets; VS Code uses UTF-16. */
export function normalizeCompilerDiagnostics(payload: unknown, text: string, file: string, cwd: string): CompilerDiagnostic[] {
  const diagnostics = record(payload).diagnostics;
  if (!Array.isArray(diagnostics)) return [];
  const bytes = Buffer.from(text, 'utf8');
  const utf16Offset = (byte: number) => bytes.subarray(0, Math.max(0, Math.min(byte, bytes.length))).toString('utf8').length;
  const result: CompilerDiagnostic[] = [];
  for (const value of diagnostics) {
    const item = record(value);
    const position = record(item.position);
    if (typeof position.source === 'string' && position.source && path.resolve(cwd, position.source) !== path.resolve(file)) continue;
    const start = typeof position.start_byte === 'number' ? utf16Offset(position.start_byte) : typeof item.start === 'number' ? item.start : 0;
    const end = typeof position.end_byte === 'number' ? utf16Offset(position.end_byte) : typeof item.end === 'number' ? item.end : start + 1;
    result.push({
      start: Math.max(0, Math.min(start, text.length)), end: Math.max(start + 1, Math.min(end, text.length)),
      severity: typeof item.severity === 'string' ? item.severity : undefined,
      code: typeof item.code === 'string' || typeof item.code === 'number' ? item.code : undefined,
      message: typeof item.message === 'string' ? item.message : undefined,
    });
  }
  return result;
}
