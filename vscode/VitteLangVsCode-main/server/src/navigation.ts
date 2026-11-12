import { scanLexical } from "./lexical.js";
// navigation.ts — symboles, définitions, références, rename, workspace symbols
// Version enrichie :
// - Parsing tolérant qui ignore commentaires/chaînes
// - Hiérarchie de symboles via pile d’accolades
// - Règles étendues alignées sur la syntaxe Vitte actuelle
// - Références/rename hors commentaires/chaînes
// - Cache par document/version
// - Workspace symbols avec fuzzy match et scoring stable
// - API additionnelle: prepareRename, indexDocument

import type { TextDocument } from "vscode-languageserver-textdocument";
import {
  Location,
  Position,
  Range,
  SymbolKind,
} from "vscode-languageserver/node";
import type {
  CallHierarchyIncomingCall,
  CallHierarchyItem,
  CallHierarchyOutgoingCall,
  DocumentSymbol,
  TypeHierarchyItem,
  TypeHierarchySubtypesParams,
  TypeHierarchySupertypesParams,
  WorkspaceSymbol,
} from "vscode-languageserver/node";

/* --------------------------------- Types ---------------------------------- */

interface FlatSymbol {
  name: string;
  kind: SymbolKind;
  range: Range;           // entier bloc (ex: de 'proc' à la fin de la signature/ligne)
  selectionRange: Range;  // le nom seul
  containerName?: string; // parent logique (space/form/trait)
}

interface DocIndex {
  flat: FlatSymbol[];
  outline: DocumentSymbol[];
  byName: Map<string, FlatSymbol[]>;
}

/* -------------------------- Masque code (lexer light) ---------------------- */
// Construit un masque booléen de positions « valides » (hors commentaires et chaînes)
// Supporte //, /* */, "...", '...', r"..." et r#"..."# (style Rust/Vitte). Sans interpolation.

export function buildCodeMask(text: string): Uint8Array {
  return scanLexical(text).mask;
}

function isIdentChar(ch: number): boolean {
  return (ch >= 48 && ch <= 57) || // 0-9
         (ch >= 65 && ch <= 90) || // A-Z
         (ch >= 97 && ch <= 122) || // a-z
         ch === 95; // _
}

function isPathChar(ch: number): boolean {
  return isIdentChar(ch) || ch === 0x2f /* / */ || ch === 0x2e /* . */ || ch === 0x3a /* : */ || ch === 0x2d /* - */;
}

function wordAt(doc: TextDocument, pos: Position): string | null {
  const text = doc.getText();
  const off = doc.offsetAt(pos);
  let s = off, e = off;
  while (s > 0 && isIdentChar(text.charCodeAt(s - 1))) s--;
  while (e < text.length && isIdentChar(text.charCodeAt(e))) e++;
  return e > s ? text.slice(s, e) : null;
}

function tokenAt(doc: TextDocument, pos: Position): string | null {
  const text = doc.getText();
  const off = doc.offsetAt(pos);
  let s = off, e = off;
  while (s > 0 && isPathChar(text.charCodeAt(s - 1))) s--;
  while (e < text.length && isPathChar(text.charCodeAt(e))) e++;
  return e > s ? text.slice(s, e) : null;
}

function escapeRx(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function posKey(p: Position): string { return `${p.line}:${p.character}`; }

function dedupeBy<T>(arr: T[], keyFn: (t: T) => string): T[] {
  const seen = new Set<string>();
  const out: T[] = [];
  for (const v of arr) {
    const k = keyFn(v);
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(v);
  }
  return out;
}

function isValidIdent(s: string): boolean { return /^[A-Za-z_]\w*$/.test(s); }

/* ------------------------------ Règles symboles ---------------------------- */
// Les regex sont évaluées uniquement sur les positions mask==1

const RULES: { rx: RegExp; kind: SymbolKind; nameGroup: number; containerHint?: (m: RegExpExecArray) => string | undefined }[] = [
  { rx: /\bspace\s+([A-Za-z_][\w./:]*)/g,                               kind: SymbolKind.Namespace, nameGroup: 1 },
  { rx: /\buse\s+([A-Za-z_][\w./:]*)/g,                                 kind: SymbolKind.Namespace, nameGroup: 1 },
  { rx: /\bpull\s+([A-Za-z_][\w./:]*)/g,                                kind: SymbolKind.Namespace, nameGroup: 1 },
  { rx: /\bentry\s+([A-Za-z_]\w*)\s+at\s+[A-Za-z_][\w./:]*/g,           kind: SymbolKind.Function, nameGroup: 1 },
  { rx: /\bentry\s+[A-Za-z_]\w*\s+at\s+([A-Za-z_][\w./:]*)/g,           kind: SymbolKind.Namespace, nameGroup: 1 },
  { rx: /\bshare\s+([A-Za-z_]\w*)/g,                                     kind: SymbolKind.Variable, nameGroup: 1 },
  { rx: /\bproc\s+([A-Za-z_]\w*)\s*\(/g,                                kind: SymbolKind.Function, nameGroup: 1 },
  { rx: /\bform\s+([A-Za-z_]\w*)/g,                                     kind: SymbolKind.Struct, nameGroup: 1 },
  { rx: /\bpick\s+([A-Za-z_]\w*)/g,                                     kind: SymbolKind.Enum, nameGroup: 1 },
  { rx: /\btrait\s+([A-Za-z_]\w*)/g,                                    kind: SymbolKind.Interface, nameGroup: 1 },
  { rx: /\btype\s+([A-Za-z_]\w*)/g,                                     kind: SymbolKind.TypeParameter, nameGroup: 1 },
  { rx: /\bconst\s+([A-Za-z_]\w*)/g,                                    kind: SymbolKind.Constant, nameGroup: 1 },
  { rx: /\b(?:let|make)\s+([A-Za-z_]\w*)/g,                             kind: SymbolKind.Variable, nameGroup: 1 },
  { rx: /\b(?:field|case)\s+([A-Za-z_]\w*)/g,                           kind: SymbolKind.Field, nameGroup: 1 },
];

function* iterMaskedMatches(rx: RegExp, text: string, mask: Uint8Array): Generator<RegExpExecArray> {
  rx.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = rx.exec(text))) {
    const idx = m.index ?? 0;
    // Vérifie que le début du match tombe sur zone code
    if (mask[idx]) {
      yield m;
    }
    if (m[0].length === 0) rx.lastIndex++;
  }
}

/* ------------------------ Collecte + hiérarchie outline -------------------- */

function collectFlatSymbols(doc: TextDocument): FlatSymbol[] {
  const text = doc.getText();
  const mask = buildCodeMask(text);
  const out: FlatSymbol[] = [];

  for (const { rx, kind, nameGroup } of RULES) {
    for (const m of iterMaskedMatches(rx, text, mask)) {
      const name = m[nameGroup];
      if (!name) continue;
      const start = doc.positionAt(m.index ?? 0);
      const end = doc.positionAt((m.index ?? 0) + m[0].length);
      const nameOffset = (m.index ?? 0) + m[0].indexOf(name);
      const nameStart = doc.positionAt(nameOffset);
      const nameEnd = doc.positionAt(nameOffset + name.length);
      out.push({
        name,
        kind,
        range: Range.create(start, end),
        selectionRange: Range.create(nameStart, nameEnd),
      });
    }
  }

  // Déduplication conservatrice
  return dedupeBy(out, s => `${s.kind}:${s.name}:${posKey(s.selectionRange.start)}`);
}

function buildOutline(doc: TextDocument, flat: FlatSymbol[]): DocumentSymbol[] {
  // Trie par position pour associer via une pile d’accolades
  const text = doc.getText();
  type Node = DocumentSymbol & { children: DocumentSymbol[] };
  const nodes: Node[] = flat
    .map(s => ({
      name: s.name,
      kind: s.kind,
      range: s.range,
      selectionRange: s.selectionRange,
      children: [],
    }))
    .sort((a, b) => {
      const da = doc.offsetAt(a.range.start);
      const db = doc.offsetAt(b.range.start);
      return da - db;
    });

  // Pile de conteneurs basée sur accolades
  const root: Node = { name: "<root>", kind: SymbolKind.Namespace, range: Range.create(Position.create(0,0), Position.create(0,0)), selectionRange: Range.create(Position.create(0,0), Position.create(0,0)), children: [] };
  const stack: Node[] = [root];

  // Prépare positions des '{' et '}' sur zones code
  const mask = buildCodeMask(text);
  const opens: number[] = [];
  for (let i = 0; i < text.length; i++) {
    if (!mask[i]) continue;
    const ch = text.charCodeAt(i);
    if (ch === 0x7b /* { */) opens.push(i);
    else if (ch === 0x7d /* } */) {
      if (opens.length) opens.pop();
    }
  }
  // Index rapide: map startOffset->depth at that point
  // Simplification: nous utilisons l’ordre croissant + heuristique par proximité

  function pushToBestContainer(node: Node) {
    // Heuristique: rattacher au dernier symbole dont la position est avant node et qui n’est pas clos avant.
    for (let j = stack.length - 1; j >= 0; j--) {
      const top = stack[j];
      const topStart = doc.offsetAt(top.range.start);
      const nodeStart = doc.offsetAt(node.range.start);
      if (nodeStart >= topStart) {
        top.children.push(node);
        stack.push(node);
        return;
      }
    }
    root.children.push(node);
    stack.push(node);
  }

  // On parcourt les nodes et simule la fermeture quand on rencontre '}' avant prochain node
  const bracesPositions: number[] = [];
  const n = text.length;
  for (let i = 0; i < n; i++) if (mask[i] && (text[i] === '{' || text[i] === '}')) bracesPositions.push(i);

  let bp = 0;
  for (const node of nodes) {
    const nodeStart = doc.offsetAt(node.range.start);
    // ferme conteneurs jusqu’à la position courante
    while (bp < bracesPositions.length && bracesPositions[bp] < nodeStart) {
      const ch = text[bracesPositions[bp]];
      if (ch === '}') {
        if (stack.length > 1) stack.pop();
      }
      bp++;
    }
    pushToBestContainer(node);
  }
  // Ferme tout ce qui reste
  while (bp < bracesPositions.length) {
    if (text[bracesPositions[bp]] === '}' && stack.length > 1) stack.pop();
    bp++;
  }

  return root.children;
}

/* ------------------------------- Index & cache ----------------------------- */

const docCache = new WeakMap<TextDocument, { version: number; index: DocIndex }>();

function indexDocument(doc: TextDocument): DocIndex {
  const cached = docCache.get(doc);
  if (cached && cached.version === doc.version) return cached.index;

  const flat = collectFlatSymbols(doc);
  const outline = buildOutline(doc, flat);
  const byName = new Map<string, FlatSymbol[]>();
  for (const s of flat) {
    const arr = byName.get(s.name) ?? [];
    arr.push(s);
    byName.set(s.name, arr);
  }
  const index = { flat, outline, byName };
  docCache.set(doc, { version: doc.version, index });
  return index;
}

/* --------------------------------- API doc --------------------------------- */

export function documentSymbols(doc: TextDocument): DocumentSymbol[] {
  return indexDocument(doc).outline;
}

export function symbolOutline(doc: TextDocument): DocumentSymbol[] {
  return documentSymbols(doc);
}

/* --------------------------- Définitions / refs ---------------------------- */

export function definitionAtPosition(doc: TextDocument, pos: Position, uri: string): Location[] {
  const tok = tokenAt(doc, pos) ?? wordAt(doc, pos);
  if (!tok) return [];
  const pathLike = /[./:-]/.test(tok);
  const { byName } = indexDocument(doc);
  const defs = pathLike
    ? indexDocument(doc).flat.filter((s) => s.kind === SymbolKind.Namespace && (s.name === tok || s.name.endsWith(`/${tok}`) || s.name.endsWith(`.${tok}`)))
    : (byName.get(tok) ?? []);
  // Tri par priorité de kind puis proximité
  const baseOff = doc.offsetAt(pos);
  defs.sort((a, b) => kindPriority(a.kind) - kindPriority(b.kind) || Math.abs(doc.offsetAt(a.selectionRange.start) - baseOff) - Math.abs(doc.offsetAt(b.selectionRange.start) - baseOff));
  return defs.map(d => Location.create(uri, d.selectionRange));
}

export function referencesAtPosition(doc: TextDocument, pos: Position, uri: string): Location[] {
  const tok = tokenAt(doc, pos) ?? wordAt(doc, pos);
  if (!tok) return [];
  const pathLike = /[./:-]/.test(tok);
  const text = doc.getText();
  const mask = buildCodeMask(text);
  const out: Location[] = [];

  const re = pathLike
    ? new RegExp(`(?<![A-Za-z0-9_./:-])${escapeRx(tok)}(?![A-Za-z0-9_./:-])`, "g")
    : new RegExp(`(?<![A-Za-z0-9_])${escapeRx(tok)}(?![A-Za-z0-9_])`, "g");
  re.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = re.exec(text))) {
    const idx = m.index ?? 0;
    if (!mask[idx]) { if (m[0].length === 0) re.lastIndex++; continue; }
    const start = doc.positionAt(idx);
    const end = doc.positionAt(idx + m[0].length);
    out.push(Location.create(uri, Range.create(start, end)));
    if (m[0].length === 0) re.lastIndex++;
  }
  return out;
}

/* --------------------------------- Rename ---------------------------------- */

export function prepareRename(doc: TextDocument, pos: Position): { range: Range; placeholder: string } | null {
  const name = wordAt(doc, pos);
  if (!name || !isValidIdent(name)) return null;
  const off = doc.offsetAt(pos);
  const start = doc.positionAt(off - (name.length - (name.lastIndexOf(name) + 1)));
  const end = doc.positionAt(doc.offsetAt(start) + name.length);
  return { range: Range.create(start, end), placeholder: name };
}

export function renameSymbol(doc: TextDocument, pos: Position, newName: string): { range: Range; newText: string }[] {
  const old = wordAt(doc, pos);
  if (!old || !isValidIdent(newName)) return [];
  return renameIdentifierByName(doc, old, newName);
}

export function renameIdentifierByName(doc: TextDocument, oldName: string, newName: string): { range: Range; newText: string }[] {
  if (!isValidIdent(oldName) || !isValidIdent(newName)) return [];
  const text = doc.getText();
  const mask = buildCodeMask(text);
  const edits: { range: Range; newText: string }[] = [];
  const re = new RegExp(`(?<![A-Za-z0-9_])${escapeRx(oldName)}(?![A-Za-z0-9_])`, "g");
  let m: RegExpExecArray | null;
  while ((m = re.exec(text))) {
    const idx = m.index ?? 0;
    if (!mask[idx]) { if (m[0].length === 0) re.lastIndex++; continue; }
    const start = doc.positionAt(idx);
    const end = doc.positionAt(idx + m[0].length);
    edits.push({ range: Range.create(start, end), newText: newName });
    if (m[0].length === 0) re.lastIndex++;
  }
  return edits;
}

export function tokenAtPosition(doc: TextDocument, pos: Position): string | null {
  return tokenAt(doc, pos) ?? wordAt(doc, pos);
}

/* --------------------------- Call/Type hierarchy --------------------------- */

function fullRangeForLine(doc: TextDocument, line: number): Range {
  const start = Position.create(line, 0);
  const end = Position.create(line, Number.MAX_SAFE_INTEGER);
  const text = doc.getText(Range.create(start, end));
  return Range.create(start, Position.create(line, text.length));
}

export function prepareCallHierarchy(doc: TextDocument, pos: Position, uri: string): CallHierarchyItem[] {
  const name = wordAt(doc, pos);
  if (!name) return [];
  const text = doc.getText();
  const rx = new RegExp(`\\b(?:proc|entry)\\s+${escapeRx(name)}\\b`, "g");
  let m: RegExpExecArray | null;
  const out: CallHierarchyItem[] = [];
  while ((m = rx.exec(text))) {
    const off = (m.index ?? 0) + m[0].lastIndexOf(name);
    const s = doc.positionAt(off);
    out.push({
      name,
      kind: SymbolKind.Function,
      uri,
      range: fullRangeForLine(doc, s.line),
      selectionRange: Range.create(s, Position.create(s.line, s.character + name.length)),
    });
    if (m[0].length === 0) rx.lastIndex++;
  }
  return dedupeBy(out, (i) => `${i.uri}:${i.selectionRange.start.line}:${i.selectionRange.start.character}:${i.name}`);
}

function findCallSites(doc: TextDocument, name: string): Range[] {
  const text = doc.getText();
  const mask = buildCodeMask(text);
  const rx = new RegExp(`(?<![A-Za-z0-9_])${escapeRx(name)}\\s*\\(`, "g");
  const out: Range[] = [];
  let m: RegExpExecArray | null;
  while ((m = rx.exec(text))) {
    const idx = m.index ?? 0;
    if (!mask[idx]) continue;
    const s = doc.positionAt(idx);
    out.push(Range.create(s, Position.create(s.line, s.character + name.length)));
    if (m[0].length === 0) rx.lastIndex++;
  }
  return out;
}

function nearestOwnerFn(doc: TextDocument, line: number): { name: string; line: number } | null {
  const lines = doc.getText().split(/\r?\n/);
  for (let i = line; i >= 0; i--) {
    const m = /^\s*(?:proc|entry)\s+([A-Za-z_]\w*)\b/.exec(lines[i] ?? "");
    if (m?.[1]) return { name: m[1], line: i };
  }
  return null;
}

export function callHierarchyIncoming(doc: TextDocument, item: CallHierarchyItem): CallHierarchyIncomingCall[] {
  const refs = findCallSites(doc, item.name);
  const byCaller = new Map<string, { caller: CallHierarchyItem; fromRanges: Range[] }>();
  for (const r of refs) {
    const owner = nearestOwnerFn(doc, r.start.line);
    if (!owner) continue;
    const key = `${owner.name}:${owner.line}`;
    let entry = byCaller.get(key);
    if (!entry) {
      const ownerStart = Position.create(owner.line, 0);
      entry = {
        caller: {
          name: owner.name,
          kind: SymbolKind.Function,
          uri: item.uri,
          range: fullRangeForLine(doc, owner.line),
          selectionRange: Range.create(ownerStart, Position.create(owner.line, owner.name.length)),
        },
        fromRanges: [],
      };
      byCaller.set(key, entry);
    }
    entry.fromRanges.push(r);
  }
  return Array.from(byCaller.values()).map((x) => ({ from: x.caller, fromRanges: x.fromRanges }));
}

function calledFnsInBody(doc: TextDocument, ownerLine: number): string[] {
  const lines = doc.getText().split(/\r?\n/);
  const start = ownerLine;
  const max = Math.min(lines.length - 1, ownerLine + 80);
  const out = new Set<string>();
  const rx = /\b([A-Za-z_]\w*)\s*\(/g;
  for (let i = start; i <= max; i++) {
    const line = lines[i] ?? "";
    if (i > start && /^\s*(?:proc|entry)\b/.test(line)) break;
    let m: RegExpExecArray | null;
    while ((m = rx.exec(line))) {
      const name = m[1];
      if (!name || name === "if" || name === "for" || name === "match") continue;
      out.add(name);
      if (m[0].length === 0) rx.lastIndex++;
    }
    rx.lastIndex = 0;
  }
  return Array.from(out);
}

export function callHierarchyOutgoing(doc: TextDocument, item: CallHierarchyItem): CallHierarchyOutgoingCall[] {
  const owner = nearestOwnerFn(doc, item.selectionRange.start.line);
  if (!owner) return [];
  const calls = calledFnsInBody(doc, owner.line);
  const out: CallHierarchyOutgoingCall[] = [];
  for (const name of calls) {
    const targets = prepareCallHierarchy(doc, Position.create(owner.line, 0), item.uri)
      .filter((i) => i.name === name);
    if (targets.length === 0) continue;
    const fromRanges = findCallSites(doc, name).filter((r) => r.start.line >= owner.line);
    out.push({ to: targets[0], fromRanges });
  }
  return out;
}

export function prepareTypeHierarchy(doc: TextDocument, pos: Position, uri: string): TypeHierarchyItem[] {
  const name = wordAt(doc, pos);
  if (!name) return [];
  const text = doc.getText();
  const rx = new RegExp(`\\b(?:type|form|trait|pick)\\s+${escapeRx(name)}\\b`, "g");
  let m: RegExpExecArray | null;
  const out: TypeHierarchyItem[] = [];
  while ((m = rx.exec(text))) {
    const off = (m.index ?? 0) + m[0].lastIndexOf(name);
    const s = doc.positionAt(off);
    out.push({
      name,
      kind: SymbolKind.Class,
      uri,
      range: fullRangeForLine(doc, s.line),
      selectionRange: Range.create(s, Position.create(s.line, s.character + name.length)),
    });
    if (m[0].length === 0) rx.lastIndex++;
  }
  return out;
}

export function typeHierarchySupertypes(doc: TextDocument, params: TypeHierarchySupertypesParams): TypeHierarchyItem[] {
  const item = params.item;
  const lines = doc.getText().split(/\r?\n/);
  const line = lines[item.selectionRange.start.line] ?? "";
  const out: TypeHierarchyItem[] = [];
  const m1 = /\b(?:type|trait|form|pick)\s+[A-Za-z_]\w*\s+as\s+([A-Za-z_]\w*)/.exec(line);
  const m2 = /\bis\s+([A-Za-z_]\w*)/.exec(line);
  for (const candidate of [m1?.[1], m2?.[1]]) {
    if (!candidate) continue;
    out.push({
      name: candidate,
      kind: SymbolKind.Interface,
      uri: item.uri,
      range: item.range,
      selectionRange: item.selectionRange,
    });
  }
  return out;
}

export function typeHierarchySubtypes(doc: TextDocument, params: TypeHierarchySubtypesParams): TypeHierarchyItem[] {
  const name = params.item.name;
  const text = doc.getText();
  const rx = new RegExp(`\\b(?:type|trait|form|pick)\\s+([A-Za-z_]\\w*)\\s+as\\s+${escapeRx(name)}\\b`, "g");
  let m: RegExpExecArray | null;
  const out: TypeHierarchyItem[] = [];
  while ((m = rx.exec(text))) {
    const sub = m[1];
    if (!sub) continue;
    const off = (m.index ?? 0) + m[0].indexOf(sub);
    const s = doc.positionAt(off);
    out.push({
      name: sub,
      kind: SymbolKind.Class,
      uri: params.item.uri,
      range: fullRangeForLine(doc, s.line),
      selectionRange: Range.create(s, Position.create(s.line, s.character + sub.length)),
    });
    if (m[0].length === 0) rx.lastIndex++;
  }
  return out;
}

/* ----------------------------- Workspace symbols --------------------------- */

export function workspaceSymbols(
  query: string,
  openDocs: { uri: string; doc: TextDocument }[],
  limit = 200
): WorkspaceSymbol[] {
  const q = query.trim();
  const result: { ws: WorkspaceSymbol; score: number; idx: number }[] = [];
  let seq = 0;
  for (const { uri, doc } of openDocs) {
    const { flat } = indexDocument(doc);
    for (const s of flat) {
      const score = q ? fuzzyScore(s.name, q) : 1;
      if (q && score <= 0) continue;
      result.push({
        ws: { name: s.name, kind: s.kind, location: Location.create(uri, s.selectionRange) },
        score,
        idx: seq++,
      });
      if (result.length >= limit * 4) break; // cap intermédiaire avant tri
    }
  }
  result.sort((a, b) => b.score - a.score || a.idx - b.idx);
  return result.slice(0, limit).map(r => r.ws);
}

/* --------------------------------- Helpers -------------------------------- */

function kindPriority(k: SymbolKind): number {
  switch (k) {
    case SymbolKind.Function: return 0;
    case SymbolKind.Method: return 1;
    case SymbolKind.Variable: return 2;
    case SymbolKind.Constant: return 3;
    case SymbolKind.Struct: return 4;
    case SymbolKind.Class: return 5;
    case SymbolKind.Enum: return 6;
    case SymbolKind.Interface: return 7;
    case SymbolKind.Namespace: return 8;
    case SymbolKind.Field: return 9;
    default: return 10;
  }
}

function fuzzyScore(name: string, query: string): number {
  // subsequence scoring sensible à la casse avec bonus pour débuts de mots et camelCase
  let i = 0, j = 0, score = 0, lastMatch = -2;
  while (i < name.length && j < query.length) {
    const nc = name[i];
    const qc = query[j];
    if (nc.toLowerCase() === qc.toLowerCase()) {
      let s = 1;
      if (i === 0 || !isIdentChar(name.charCodeAt(i - 1))) s += 2; // début de mot
      if (nc === qc) s += 1; // casse exacte
      if (i === lastMatch + 1) s += 2; // consécutif
      score += s;
      lastMatch = i;
      j++; i++;
    } else {
      i++;
    }
  }
  return j === query.length ? score : -1;
}

/* --------------------------------- Exports -------------------------------- */

export function indexDocForTests(doc: TextDocument): DocIndex { return indexDocument(doc); }
