import type {
  ParameterInformation,
  Position,
  SignatureHelp,
  SignatureInformation,
} from "vscode-languageserver/node";
import type { TextDocument } from "vscode-languageserver-textdocument";
import { buildCodeMask } from "./navigation.js";

interface ProcedureSignature {
  name: string;
  information: SignatureInformation;
}

export function provideSignatureHelp(doc: TextDocument, position: Position): SignatureHelp | undefined {
  const text = doc.getText();
  const cursor = doc.offsetAt(position);
  const mask = buildCodeMask(text);
  const code = maskText(text, mask);
  const openParen = findActiveCallParen(text, mask, cursor);
  if (openParen === undefined) return undefined;

  const callPrefix = code.slice(0, openParen);
  if (/\b(?:proc|intrinsic)\s+[A-Za-z_]\w*(?:\s*<[^{}]*>)?\s*$/.test(callPrefix)) {
    return undefined;
  }

  const callMatch = /([A-Za-z_]\w*(?:(?:::|\.)[A-Za-z_]\w*)*)\s*$/.exec(callPrefix);
  const qualifiedName = callMatch?.[1];
  const nameParts = qualifiedName?.split(/::|\./);
  const calledName = nameParts?.[nameParts.length - 1];
  if (!calledName) return undefined;

  const signatures = findProcedureSignatures(code, mask)
    .filter((signature) => signature.name === calledName)
    .map((signature) => signature.information);
  if (signatures.length === 0) return undefined;

  const activeParameter = countActiveParameter(text, mask, openParen + 1, cursor);
  const parameterCount = signatures[0]?.parameters?.length ?? 0;
  return {
    signatures,
    activeSignature: 0,
    ...(parameterCount > 0 ? { activeParameter: Math.min(activeParameter, parameterCount - 1) } : {}),
  };
}

function findActiveCallParen(text: string, mask: Uint8Array, cursor: number): number | undefined {
  const opens: number[] = [];
  for (let i = 0; i < cursor && i < text.length; i++) {
    if (!mask[i]) continue;
    if (text[i] === "(") opens.push(i);
    else if (text[i] === ")" && opens.length > 0) opens.pop();
  }
  return opens[opens.length - 1];
}

function countActiveParameter(text: string, mask: Uint8Array, start: number, end: number): number {
  const nested: string[] = [];
  let parameter = 0;
  for (let i = start; i < end; i++) {
    if (!mask[i]) continue;
    const character = text[i];
    if (character === "(" || character === "[" || character === "{") {
      nested.push(character);
    } else if (character === ")" || character === "]" || character === "}") {
      nested.pop();
    } else if (character === "," && nested.length === 0) {
      parameter++;
    }
  }
  return parameter;
}

function findProcedureSignatures(code: string, mask: Uint8Array): ProcedureSignature[] {
  const signatures: ProcedureSignature[] = [];
  const declaration = /\b(?:proc|intrinsic)\s+([A-Za-z_]\w*)\b/g;
  let match: RegExpExecArray | null;

  while ((match = declaration.exec(code))) {
    const name = match[1];
    if (!name) continue;
    let cursor = declaration.lastIndex;
    while (/\s/.test(code[cursor] ?? "")) cursor++;

    if (code[cursor] === "<") {
      const genericEnd = findMatchingDelimiter(code, mask, cursor, "<", ">");
      if (genericEnd === undefined) continue;
      cursor = genericEnd + 1;
      while (/\s/.test(code[cursor] ?? "")) cursor++;
    }

    if (code[cursor] !== "(") continue;
    const closeParen = findMatchingDelimiter(code, mask, cursor, "(", ")");
    if (closeParen === undefined) continue;

    const rawParameters = splitTopLevel(code.slice(cursor + 1, closeParen));
    const parameters = rawParameters
      .map((part) => part.trim().replace(/\s+/g, " "))
      .filter(Boolean);
    const returnType = findReturnType(code, closeParen + 1);
    const label = `proc ${name}(${parameters.join(", ")})${returnType ? ` -> ${returnType}` : ""}`;
    const parameterInformation: ParameterInformation[] = [];
    let labelOffset = `proc ${name}(`.length;
    for (const parameter of parameters) {
      parameterInformation.push({ label: [labelOffset, labelOffset + parameter.length] });
      labelOffset += parameter.length + 2;
    }

    signatures.push({
      name,
      information: { label, parameters: parameterInformation },
    });
    declaration.lastIndex = closeParen + 1;
  }

  return signatures;
}

function findMatchingDelimiter(
  text: string,
  mask: Uint8Array,
  start: number,
  open: string,
  close: string,
): number | undefined {
  let depth = 0;
  for (let i = start; i < text.length; i++) {
    if (!mask[i]) continue;
    if (text[i] === open) depth++;
    else if (text[i] === close && --depth === 0) return i;
  }
  return undefined;
}

function splitTopLevel(parameters: string): string[] {
  const parts: string[] = [];
  const nested: string[] = [];
  let angleDepth = 0;
  let start = 0;

  for (let i = 0; i < parameters.length; i++) {
    const character = parameters[i];
    if (character === "(" || character === "[" || character === "{") nested.push(character);
    else if (character === ")" || character === "]" || character === "}") nested.pop();
    else if (nested.length === 0 && character === "<") angleDepth++;
    else if (nested.length === 0 && character === ">" && angleDepth > 0) angleDepth--;
    else if (character === "," && nested.length === 0 && angleDepth === 0) {
      parts.push(parameters.slice(start, i));
      start = i + 1;
    }
  }

  if (start < parameters.length) parts.push(parameters.slice(start));
  return parts;
}

function findReturnType(code: string, start: number): string | undefined {
  let cursor = start;
  while (/\s/.test(code[cursor] ?? "")) cursor++;
  if (!code.startsWith("->", cursor)) return undefined;
  cursor += 2;
  while (/\s/.test(code[cursor] ?? "")) cursor++;

  const typeStart = cursor;
  const nested: string[] = [];
  let angleDepth = 0;
  while (cursor < code.length) {
    const character = code[cursor];
    if (character === "(" || character === "[" || character === "{") {
      if (character === "{" && nested.length === 0 && angleDepth === 0) break;
      nested.push(character);
    } else if (character === ")" || character === "]" || character === "}") {
      nested.pop();
    } else if (nested.length === 0 && character === "<") {
      angleDepth++;
    } else if (nested.length === 0 && character === ">" && angleDepth > 0) {
      angleDepth--;
    } else if (nested.length === 0 && angleDepth === 0 && character === ";") {
      break;
    } else if (
      nested.length === 0 &&
      angleDepth === 0 &&
      /\b(?:where|requires|ensures)\b/.test(code.slice(cursor))
    ) {
      break;
    }
    cursor++;
  }

  return code.slice(typeStart, cursor).trim() || undefined;
}

function maskText(text: string, mask: Uint8Array): string {
  return text.split("").map((character, index) =>
    mask[index] || character === "\n" || character === "\r" ? character : " ",
  ).join("");
}
