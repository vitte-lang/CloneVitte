import { maskNonCode } from "./lexical.js";
// indexer.ts — indexation robuste des symboles pour Vitte/Vit (LSP prêt, sans dépendance runtime)

// Import minimal côté serveur LSP.
import { TextDocument } from "vscode-languageserver-textdocument";
import type {
  DocumentSymbol as LspDocumentSymbol,
  Location as LspLocation,
  Position as LspPosition,
  Range as LspRange,
  SymbolInformation as LspSymbolInformation,
  SymbolKind as LspSymbolKind,
} from "vscode-languageserver-types";

/* ============================================================================
 * Types et constantes
 * ========================================================================== */

// Utiliser "enum" plutôt que "const enum" pour éviter les soucis de transpilation TS dans certains toolchains.
export enum SK {
  Module = 2,
  Namespace = 3,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
  Struct = 23,
  TypeParameter = 26,
}

export interface IndexedSymbol {
  name: string;
  kind: SK;
  uri: string;
  line: number;
  character: number;
  endLine?: number;
  endCharacter?: number;
  containerName?: string;
}

type Uri = string;

/** Index global: uri -> liste de symboles */
const INDEX = new Map<Uri, IndexedSymbol[]>();
const DOC_HASH = new Map<Uri, number>();

export interface IndexSnapshot {
  version: number;
  entries: { uri: string; hash: number; symbols: IndexedSymbol[] }[];
}

interface DeclarationInfo {
  kind: SK;
  expectBody: boolean;
  allowBodyless?: boolean;
}

interface PendingBody {
  symbolIndex: number;
  allowBodyless: boolean;
  startOffset: number;
  bodyOffset?: number;
}

type BodyExpectation =
  | { kind: "body"; offset: number }
  | { kind: "none" }
  | { kind: "unknown" };

const DECLARATIONS = new Map<string, DeclarationInfo>([
  ["space", { kind: SK.Namespace, expectBody: true, allowBodyless: true }],
  ["form", { kind: SK.Struct, expectBody: true }],
  ["pick", { kind: SK.Enum, expectBody: true }],
  ["case", { kind: SK.Field, expectBody: true, allowBodyless: true }],
  ["trait", { kind: SK.Interface, expectBody: true }],
  ["type", { kind: SK.Interface, expectBody: false }],
  ["proc", { kind: SK.Function, expectBody: true, allowBodyless: true }],
  ["const", { kind: SK.Constant, expectBody: false }],
  ["field", { kind: SK.Field, expectBody: false }],
]);

const MODIFIERS = new Set<string>();

class SymbolParser {
  private readonly text: string;
  private readonly masked: string;
  private readonly length: number;
  private readonly symbols: IndexedSymbol[] = [];
  private readonly braceStack: { symbolIndex: number | null; offset: number }[] = [];
  private readonly pendingBodies: PendingBody[] = [];
  private readonly activeSymbols: number[] = [];
  private pos = 0;

  constructor(private readonly doc: TextDocument) {
    this.text = doc.getText();
    this.masked = maskNonCode(this.text);
    this.length = this.masked.length;
  }

  parse(): IndexedSymbol[] {
    while (this.pos < this.length) {
      const ch = this.masked[this.pos];

      if (isWhitespace(ch)) {
        this.pos++;
        continue;
      }

      if (ch === "#") {
        this.skipAttribute();
        continue;
      }

      if (ch === "{") {
        this.handleOpenBrace();
        this.pos++;
        continue;
      }

      if (ch === "}") {
        this.handleCloseBrace();
        this.pos++;
        continue;
      }

      if (ch === ";") {
        this.handleSemicolon();
        this.pos++;
        continue;
      }

      if (isIdentifierStart(ch)) {
        const keywordStart = this.pos;
        const word = this.readWord();
        if (!word) {
          this.pos++;
          continue;
        }

        const lower = word.toLowerCase();

        if (MODIFIERS.has(lower)) {
          this.skipModifierTail(lower);
          continue;
        }

        const decl = DECLARATIONS.get(lower);
        if (decl) {
          this.handleDeclaration(lower, keywordStart, decl);
          continue;
        }

        continue;
      }

      this.pos++;
    }

    return this.symbols;
  }

  private handleDeclaration(keyword: string, startOffset: number, info: DeclarationInfo): void {
    const name = this.readSymbolName(keyword);
    if (!name) return;

    const startPos = this.doc.positionAt(startOffset);
    const sym: IndexedSymbol = {
      name,
      kind: info.kind,
      uri: this.doc.uri,
      line: startPos.line,
      character: startPos.character,
    };

    const container = this.currentContainer();
    if (container) {
      sym.containerName = container.name;
    }

    const index = this.symbols.push(sym) - 1;

    if (info.expectBody) {
      const expectation = this.probeBodyOffset(info, this.pos);
      if (expectation.kind === "body") {
        this.pendingBodies.push({
          symbolIndex: index,
          allowBodyless: !!info.allowBodyless,
          startOffset,
          bodyOffset: expectation.offset,
        });
      } else if (expectation.kind === "unknown") {
        this.pendingBodies.push({
          symbolIndex: index,
          allowBodyless: !!info.allowBodyless,
          startOffset,
        });
      }
    }
  }

  private readSymbolName(keyword: string): string | undefined {
    switch (keyword) {
      case "space":
        return this.readQualifiedName();
      case "form":
      case "pick":
      case "case":
      case "type":
      case "trait":
      case "field":
        this.skipTrivia();
        return this.readIdentifierToken();
      case "proc":
        this.skipTrivia();
        return this.readFunctionName();
      case "const":
        return this.readConstName();
      default:
        return undefined;
    }
  }

  private readQualifiedName(): string | undefined {
    this.skipTrivia();
    const start = this.pos;
    while (this.pos < this.length) {
      const ch = this.masked[this.pos];
      if (isIdentifierPart(ch) || ch === ":" || ch === "/" || ch === ".") {
        this.pos++;
        continue;
      }
      break;
    }
    const raw = this.text.slice(start, this.pos).trim();
    return raw ? raw : undefined;
  }

  private readFunctionName(): string | undefined {
    return this.readIdentifierToken();
  }

  private readConstName(): string | undefined {
    this.skipTrivia();
    return this.readIdentifierToken();
  }

  private readIdentifierToken(): string | undefined {
    if (!isIdentifierStart(this.masked[this.pos])) return undefined;
    const start = this.pos;
    this.pos++;
    while (this.pos < this.length && isIdentifierPart(this.masked[this.pos])) {
      this.pos++;
    }
    const raw = this.text.slice(start, this.pos);
    const trimmed = raw.trim();
    return trimmed ? trimmed : undefined;
  }

  private readWord(): string {
    const start = this.pos;
    while (this.pos < this.length && isIdentifierPart(this.masked[this.pos])) {
      this.pos++;
    }
    return this.masked.slice(start, this.pos);
  }

  private skipModifierTail(word: string): void {
    if (word === "pub") {
      this.skipTrivia();
      if (this.masked[this.pos] === "(") {
        this.pos = this.skipBalancedFrom(this.pos, "(", ")");
        this.skipTrivia();
      }
    }
  }

  private skipBalancedFrom(index: number, open: string, close: string): number {
    let depth = 0;
    let i = index;
    while (i < this.length) {
      const ch = this.masked[i];
      if (ch === open) depth++;
      else if (ch === close) {
        depth--;
        if (depth === 0) return i + 1;
      }
      i++;
    }
    return this.length;
  }

  private skipAttribute(): void {
    let i = this.pos + 1;
    if (i < this.length && this.masked[i] === "!") {
      i++;
    }
    while (i < this.length && isWhitespace(this.masked[i])) i++;
    if (i >= this.length || this.masked[i] !== "[") {
      this.pos++;
      return;
    }
    const end = this.skipBalancedFrom(i, "[", "]");
    this.pos = Math.max(end, i + 1);
  }

  private skipTrivia(): void {
    while (this.pos < this.length && isWhitespace(this.masked[this.pos])) {
      this.pos++;
    }
  }

  private currentContainer(): IndexedSymbol | undefined {
    if (!this.activeSymbols.length) return undefined;
    return this.symbols[this.activeSymbols[this.activeSymbols.length - 1]];
  }

  private handleOpenBrace(): void {
    const currentOffset = this.pos;
    let matchedIndex = -1;
    for (let i = this.pendingBodies.length - 1; i >= 0; i--) {
      const pending = this.pendingBodies[i];
      if (pending.bodyOffset !== undefined && pending.bodyOffset !== currentOffset) {
        continue;
      }
      matchedIndex = i;
      break;
    }

    let symbolIndex: number | null = null;
    if (matchedIndex >= 0) {
      const [pending] = this.pendingBodies.splice(matchedIndex, 1);
      symbolIndex = pending.symbolIndex;
      this.activeSymbols.push(symbolIndex);
    }

    this.braceStack.push({ symbolIndex, offset: currentOffset });
  }

  private handleCloseBrace(): void {
    const frame = this.braceStack.pop();
    if (!frame) return;
    if (frame.symbolIndex != null) {
      const pos = this.doc.positionAt(this.pos);
      const sym = this.symbols[frame.symbolIndex];
      sym.endLine = pos.line;
      sym.endCharacter = pos.character + 1;

      const idx = this.activeSymbols.lastIndexOf(frame.symbolIndex);
      if (idx >= 0) this.activeSymbols.splice(idx, 1);
    }
  }

  private handleSemicolon(): void {
    const current = this.pos;
    for (let i = this.pendingBodies.length - 1; i >= 0; i--) {
      const pending = this.pendingBodies[i];
      if (!pending.allowBodyless) continue;
      if (pending.bodyOffset !== undefined && pending.bodyOffset <= current) continue;
      this.pendingBodies.splice(i, 1);
      break;
    }
  }

  private matchKeywordAt(pos: number, keyword: string): boolean {
    if (pos + keyword.length > this.length) return false;
    const slice = this.masked.slice(pos, pos + keyword.length).toLowerCase();
    if (slice !== keyword) return false;

    const before = pos > 0 ? this.masked[pos - 1] : "";
    const after = pos + keyword.length < this.length ? this.masked[pos + keyword.length] : "";
    if ((before && isIdentifierPart(before)) || (after && isIdentifierPart(after))) {
      return false;
    }
    return true;
  }

  private probeBodyOffset(info: DeclarationInfo, start: number): BodyExpectation {
    let i = start;
    let depthParen = 0;
    let depthAngle = 0;
    let depthBracket = 0;

    while (i < this.length) {
      const ch = this.masked[i];
      if (ch === "(") {
        depthParen++;
        i++;
        continue;
      }
      if (ch === ")") {
        if (depthParen > 0) depthParen--;
        i++;
        continue;
      }
      if (ch === "<") {
        depthAngle++;
        i++;
        continue;
      }
      if (ch === ">") {
        if (depthAngle > 0) depthAngle--;
        i++;
        continue;
      }
      if (ch === "[") {
        depthBracket++;
        i++;
        continue;
      }
      if (ch === "]") {
        if (depthBracket > 0) depthBracket--;
        i++;
        continue;
      }
      if (isWhitespace(ch)) {
        i++;
        continue;
      }
      if (!depthParen && !depthAngle && !depthBracket) {
        if (ch === "{") return { kind: "body", offset: i };
        if (ch === ";") {
          return info.allowBodyless ? { kind: "none" } : { kind: "unknown" };
        }
        if (ch === "=") {
          return { kind: "none" };
        }
      }
      i++;
    }

    return { kind: "unknown" };
  }
}

function extract(doc: TextDocument): IndexedSymbol[] {
  const parser = new SymbolParser(doc);
  const symbols = parser.parse();
  return dedupe(symbols, (s) => `${s.kind}:${s.name}:${s.line}:${s.character}`);
}

/* ============================================================================
 * API d’index
 * ========================================================================== */

/** Indexe un document (remplace l’entrée précédente). */
export function indexDocument(doc: TextDocument): void {
  const text = doc.getText();
  const hash = fastHash(text);
  if (DOC_HASH.get(doc.uri) === hash) return;
  INDEX.set(doc.uri, extract(doc));
  DOC_HASH.set(doc.uri, hash);
}

/** Indexe une chaîne pour un uri donné. */
export function indexText(uri: string, text: string): void {
  const hash = fastHash(text);
  if (DOC_HASH.get(uri) === hash) return;
  const doc = TextDocument.create(uri, "vitte", 0, text);
  INDEX.set(uri, extract(doc));
  DOC_HASH.set(uri, hash);
}

/** Réindexe un document (alias plus explicite). */
export function updateDocument(doc: TextDocument): void {
  indexDocument(doc);
}

/** Supprime un document de l’index. */
export function removeDocument(uri: string): void {
  INDEX.delete(uri);
  DOC_HASH.delete(uri);
}

/** Vide l’index. */
export function clearIndex(): void {
  INDEX.clear();
  DOC_HASH.clear();
}

/** Récupère l’index brut (lecture seule). */
export function getIndex(): ReadonlyMap<Uri, IndexedSymbol[]> {
  return INDEX;
}

/** Récupère les symboles d’un document. */
export function getDocumentIndex(uri: string): IndexedSymbol[] {
  return INDEX.get(uri) ?? [];
}

export function exportIndexSnapshot(): IndexSnapshot {
  return {
    version: 2,
    entries: Array.from(INDEX.entries()).map(([uri, symbols]) => ({
      uri,
      hash: DOC_HASH.get(uri) ?? 0,
      symbols,
    })),
  };
}

export function loadIndexSnapshot(snapshot: IndexSnapshot | null | undefined): number {
  if (!snapshot?.entries || snapshot.version !== 2 || !Array.isArray(snapshot.entries)) return 0;
  INDEX.clear();
  DOC_HASH.clear();
  let count = 0;
  for (const entry of snapshot.entries) {
    if (!entry || typeof entry.uri !== "string" || !Array.isArray(entry.symbols)) continue;
    INDEX.set(entry.uri, entry.symbols);
    DOC_HASH.set(entry.uri, Number.isFinite(entry.hash) ? entry.hash : 0);
    count += 1;
  }
  return count;
}

/* ============================================================================
 * Requêtes et conversions LSP
 * ========================================================================== */

/** Recherche globale par fuzzy + préfixe, tri par score et nature. */
export function searchWorkspaceSymbols(query: string, limit = 400): IndexedSymbol[] {
  const q = query.trim().toLowerCase();
  const all = flattenIndex();
  if (!q) return all.slice(0, limit);

  const scored = all
    .map((s) => ({ s, score: fuzzyScore(s.name.toLowerCase(), q) }))
    .filter((x) => x.score > 0)
    .sort((a, b) =>
      b.score - a.score ||
      rankKind(b.s.kind) - rankKind(a.s.kind) ||
      a.s.name.localeCompare(b.s.name)
    )
    .slice(0, limit)
    .map((x) => x.s);

  return scored;
}

/** Conversion vers SymbolInformation[] (Workspace Symbols). */
export function toWorkspaceSymbols(syms: IndexedSymbol[]): LspSymbolInformation[] {
  return syms.map((s) => ({
    name: s.name,
    kind: toLspKind(s.kind),
    location: {
      uri: s.uri,
      range: lspRange(s.line, s.character, s.endLine ?? s.line, s.endCharacter ?? s.character),
    } as LspLocation,
    containerName: s.containerName,
  }));
}

/** Conversion hiérarchique DocumentSymbol[] pour un document. */
export function toDocumentSymbols(doc: TextDocument): LspDocumentSymbol[] {
  const list = getDocumentIndex(doc.uri);
  // Simple regroupement par containerName. Les doublons de nom sont pris tels quels.
  const byName = new Map<string, LspDocumentSymbol>();
  const roots: LspDocumentSymbol[] = [];

  for (const s of list) {
    const ds: LspDocumentSymbol = {
      name: s.name,
      kind: toLspKind(s.kind),
      range: lspRange(s.line, s.character, s.endLine ?? s.line, s.endCharacter ?? s.character),
      selectionRange: lspRange(s.line, s.character, s.line, Math.max(s.character, s.character + s.name.length)),
      children: [],
    };
    if (s.containerName && byName.has(s.containerName)) {
      byName.get(s.containerName)!.children!.push(ds);
    } else {
      roots.push(ds);
    }
    byName.set(s.name, ds);
  }
  return roots;
}

/** Renvoie les symboles à une position. */
export function symbolsAtPosition(uri: string, pos: { line: number; character: number }): IndexedSymbol[] {
  const list = getDocumentIndex(uri);
  return list.filter((s) => inRange(pos, s));
}

/** Définition naïve: symbole portant le même nom, priorisant le même fichier. */
export function findDefinition(uri: string, name: string): IndexedSymbol | undefined {
  const local = getDocumentIndex(uri).find((s) => s.name === name);
  if (local) return local;
  for (const [, list] of INDEX) {
    const hit = list.find((s) => s.name === name);
    if (hit) return hit;
  }
  return undefined;
}

/** Références naïves: symboles du même nom dans l’index. */
export function findReferences(_uri: string, name: string, limit = 500): IndexedSymbol[] {
  const acc: IndexedSymbol[] = [];
  for (const list of INDEX.values()) {
    for (const s of list) {
      if (s.name === name) {
        acc.push(s);
        if (acc.length >= limit) return acc;
      }
    }
  }
  return acc;
}

/* ============================================================================
 * Utilitaires internes
 * ========================================================================== */

function inRange(pos: { line: number; character: number }, s: IndexedSymbol): boolean {
  const startOk =
    pos.line > s.line || (pos.line === s.line && pos.character >= s.character);
  const endLine = s.endLine ?? s.line;
  const endChar = s.endCharacter ?? s.character;
  const endOk = pos.line < endLine || (pos.line === endLine && pos.character <= endChar);
  return startOk && endOk;
}

function flattenIndex(): IndexedSymbol[] {
  const acc: IndexedSymbol[] = [];
  for (const list of INDEX.values()) acc.push(...list);
  return acc;
}

function toLspKind(k: SK): LspSymbolKind {
  // Les valeurs numériques de SK sont alignées avec LSP. Cast sûr.
  return k as unknown as LspSymbolKind;
}

function lspRange(sl: number, sc: number, el: number, ec: number): LspRange {
  return {
    start: { line: sl, character: sc } as LspPosition,
    end: { line: el, character: ec } as LspPosition,
  };
}

/** Fuzzy score simple. Favorise le préfixe et les sous-séquences denses. */
function fuzzyScore(candidate: string, query: string): number {
  if (candidate === query) return 1000;
  if (candidate.startsWith(query)) return 800 - Math.min(200, candidate.length - query.length);
  let qi = 0;
  let streak = 0;
  let score = 0;
  for (let i = 0; i < candidate.length && qi < query.length; i++) {
    if (candidate[i] === query[qi]) {
      qi++; streak++; score += 5 + Math.min(10, streak);
    } else {
      streak = 0;
    }
  }
  return qi === query.length ? 300 + score : 0;
}

/** Ordre de priorité par nature de symbole. */
function rankKind(k: SK): number {
  switch (k) {
    case SK.Namespace:
    case SK.Module: return 6;
    case SK.Class:
    case SK.Struct:
    case SK.Interface:
    case SK.Enum:   return 5;
    case SK.Function:
    case SK.Method:
    case SK.Constructor: return 4;
    case SK.Property:
    case SK.Field:  return 3;
    case SK.Variable:
    case SK.Constant: return 2;
    default: return 1;
  }
}

/** Déduplication générique. */
function dedupe<T>(arr: T[], key: (t: T) => string): T[] {
  const seen = new Set<string>();
  const out: T[] = [];
  for (const x of arr) {
    const k = key(x);
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(x);
  }
  return out;
}

function isWhitespace(ch: string | undefined): boolean {
  return ch === " " || ch === "\t" || ch === "\r" || ch === "\n" || ch === "\f";
}

function isIdentifierStart(ch: string | undefined): boolean {
  if (!ch) return false;
  return /[A-Za-z_]/.test(ch);
}

function isIdentifierPart(ch: string | undefined): boolean {
  if (!ch) return false;
  return /[A-Za-z0-9_]/.test(ch);
}

function fastHash(s: string): number {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}
