import { lexicalContextAt } from "./lexical.js";
// completion.ts — complétions LSP sans dépendance externe, robustes et contextuelles

import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  CompletionItemKind,
  InsertTextFormat,
  Range,
  SymbolKind,
  MarkupKind
} from "vscode-languageserver/node";
import type {
  CompletionItem,
  Position,
  MarkupContent,
  TextEdit as LspTextEdit
} from "vscode-languageserver/node";
import type { TextEdit } from "vscode-languageserver-types";
import type { TextDocument } from "vscode-languageserver-textdocument";
import {
  BOOL_LITERALS,
  KEYWORDS,
  NIL_LITERALS,
  PRIMITIVE_TYPES,
} from "./languageFacts.js";
import { searchWorkspaceSymbols, SK } from "./indexer.js";
import { VITTE_PACKAGE_IMPORTS } from "./generated/vitteImports.js";

export type CompletionPositionContext =
  | "code"
  | "string"
  | "comment"
  | "type"
  | "import";

export interface CompletionSettings {
  enabledInComments: boolean;
  enabledInStrings: boolean;
  includeSnippets: boolean;
  workspaceSymbols: boolean;
  maxItems: number;
}

const DEFAULT_COMPLETION_SETTINGS: CompletionSettings = {
  enabledInComments: false,
  enabledInStrings: false,
  includeSnippets: true,
  workspaceSymbols: true,
  maxItems: 200,
};

let completionSettings: CompletionSettings = { ...DEFAULT_COMPLETION_SETTINGS };

export function configureCompletions(settings?: Partial<CompletionSettings>): void {
  completionSettings = {
    ...DEFAULT_COMPLETION_SETTINGS,
    ...(settings ?? {}),
    maxItems: clampNumber(settings?.maxItems, 16, 1000, DEFAULT_COMPLETION_SETTINGS.maxItems),
  };
}

/* ============================================================================
 * Tables de base
 * ========================================================================== */

// Heuristiques de membres courants, proposées après un point.
const COMMON_MEMBERS: readonly { recv: RegExp; members: string[] }[] = [
  { recv: /(str(ing)?|text)$/i, members: ["len", "is_empty()", "trim()", "to_upper()", "to_lower()", "slice(${1:start}, ${2:end})", "as_bytes()"] },
  { recv: /(vec|list|array|slice)$/i, members: ["len", "is_empty()", "push(${1:x})", "pop()", "clear()", "iter()"] },
  { recv: /(map|dict|table)$/i, members: ["len", "get(${1:key})", "set(${1:key}, ${2:val})", "remove(${1:key})", "keys()", "values()"] }
];



const BUILTIN_FUNCTIONS: readonly string[] = [
  "print",
  "println",
  "len",
  "size",
  "slice",
  "as_bytes",
  "push",
  "pop",
  "clear",
  "iter",
  "map",
  "filter",
  "reduce",
  "open",
  "read",
  "write",
  "close",
  "assert",
  "panic",
];

const SYMBOL_KIND_LABEL: Record<number, string> = {
  [SymbolKind.File]: "File",
  [SymbolKind.Module]: "Module",
  [SymbolKind.Namespace]: "Namespace",
  [SymbolKind.Package]: "Package",
  [SymbolKind.Class]: "Class",
  [SymbolKind.Method]: "Method",
  [SymbolKind.Property]: "Property",
  [SymbolKind.Field]: "Field",
  [SymbolKind.Constructor]: "Constructor",
  [SymbolKind.Enum]: "Enum",
  [SymbolKind.Interface]: "Interface",
  [SymbolKind.Function]: "Function",
  [SymbolKind.Variable]: "Variable",
  [SymbolKind.Constant]: "Constant",
  [SymbolKind.String]: "String",
  [SymbolKind.Number]: "Number",
  [SymbolKind.Boolean]: "Boolean",
  [SymbolKind.Array]: "Array",
  [SymbolKind.Object]: "Object",
  [SymbolKind.Key]: "Key",
  [SymbolKind.Null]: "Null",
  [SymbolKind.EnumMember]: "EnumMember",
  [SymbolKind.Struct]: "Struct",
  [SymbolKind.Event]: "Event",
  [SymbolKind.Operator]: "Operator",
  [SymbolKind.TypeParameter]: "TypeParameter"
};

/* ============================================================================
 * Snippets
 * ========================================================================== */

const SNIPPETS: CompletionItem[] = [
  ciSnippet("space", "Déclare un espace", "Déclare l’espace courant.",
    "space ${1:std/os/user};"),
  ciSnippet("use", "Importe un module", "Importe un module ou un symbole avec alias optionnel.",
    "use ${1:std/io}${2: as ${3:io}};"),
  ciSnippet("export", "Exporte tout le module", "Ré-exporte la surface du module courant.",
    "export *;"),
  ciSnippet("proc", "Déclare une procédure",
    "Procédure avec paramètres et type de retour optionnel.",
    "proc ${1:name}(${2:params})${3: -> ${4:Type}} {\n\t$0\n}"),
  ciSnippet("docproc", "Doc + procédure",
    "Ajoute une docstring puis une procédure.",
    "/// ${1:Summary}\n///\n/// Params:\n/// - ${2:param}: ${3:description}\n/// Returns: ${4:description}\n/// Example:\n/// ${5:example}\nproc ${6:name}(${7:params})${8: -> ${9:Type}} {\n\t$0\n}"),
  ciSnippet("form", "Déclare un form", "Structure avec des champs typés.",
    "form ${1:Name} {\n\t${2:name}: ${3:Type},\n}"),
  ciSnippet("docform", "Doc + form", "Ajoute une docstring puis un form.",
    "/// ${1:Summary}\n///\n/// Fields:\n/// - ${2:field}: ${3:description}\n/// Example:\n/// ${4:example}\nform ${5:Name} {\n\t${6:name}: ${7:Type},\n}"),
  ciSnippet("trait", "Déclare un trait", "Contrat de capacités.",
    "trait ${1:Name} {\n\t$0\n}"),
  ciSnippet("pick", "Déclare un pick", "Somme de variantes nommées.",
    "pick ${1:Name} {\n\t${2:Variant}(${3:Type}),\n\t$0\n}"),
  ciSnippet("type", "Alias de type", "Déclare un alias de type.",
    "type ${1:Alias} = ${2:Existing};"),
  ciSnippet("match", "Match expression", "Expression de branchement avec motifs.",
    "match ${1:expr} {\n\t${2:Pattern} => {\n\t\t$0\n\t},\n}"),
  ciSnippet("ifelse", "If / Else", "Structure conditionnelle complète.",
    "if ${1:cond} {\n\t${2}\n} else {\n\t${0}\n}"),
  ciSnippet("for", "Boucle for", "Boucle for-in sur un itérable.",
    "for ${1:pat} in ${2:iter} {\n\t$0\n}"),
  ciSnippet("loop", "Boucle infinie", "Boucle sans fin, utiliser break pour sortir.",
    "loop {\n\t$0\n}"),
  ciSnippet("let", "Binding local", "Déclare une variable locale.",
    "let ${1:name}${2: : ${3:Type}} = ${4:value};"),
  ciSnippet("const", "Constante", "Déclare une constante globale.",
    "const ${1:NAME}: ${2:Type} = ${3:value};"),
  ciSnippet("return", "Retour de procédure", "Retourne une valeur depuis une procédure.",
    "return ${1:value};"),
  ciSnippet("give", "Retour de procédure", "Retourne une valeur depuis une procédure.",
    "give ${1:value};"),
  ciSnippet("print", "Affichage console", "Affiche un message sur la sortie standard.",
    "println(\"${1:msg}\");"),
  ciSnippet("doc", "Doc comment", "Insert a documentation comment.",
    "/// ${1:Summary}\n///\n/// ${0:Details}")
];

/* ============================================================================
 * Extraction symbolique simple
 * ========================================================================== */

interface ExtractedSym { name: string; kind: SymbolKind; }
interface ScopedName { name: string; kind: SymbolKind; signature?: string; }
interface DeclFunction { name: string; params: string; returnType?: string; }
type InferredType = string;

function extractSymbols(doc: TextDocument): ExtractedSym[] {
  const text = doc.getText();
  const rules: { rx: RegExp; kind: SymbolKind; g: number }[] = [
    { rx: /^\s*space\s+([A-Za-z_][\w./:]*)/gm,                kind: SymbolKind.Namespace, g: 1 },
    { rx: /^\s*proc\s+([A-Za-z_]\w*)/gm,                      kind: SymbolKind.Function,  g: 1 },
    { rx: /^\s*entry\s+([A-Za-z_]\w*)\s+at\s+[A-Za-z_][\w./:]*/gm, kind: SymbolKind.Function, g: 1 },
    { rx: /^\s*form\s+([A-Za-z_]\w*)/gm,                      kind: SymbolKind.Struct,    g: 1 },
    { rx: /^\s*pick\s+([A-Za-z_]\w*)/gm,                      kind: SymbolKind.Enum,      g: 1 },
    { rx: /^\s*trait\s+([A-Za-z_]\w*)/gm,                     kind: SymbolKind.Interface, g: 1 },
    { rx: /^\s*type\s+([A-Za-z_]\w*)/gm,                      kind: SymbolKind.Interface, g: 1 },
    { rx: /^\s*const\s+([A-Za-z_]\w*)/gm,                     kind: SymbolKind.Constant,  g: 1 },
    { rx: /^\s*let\s+(?:mut\s+)?([A-Za-z_]\w*)/gm,            kind: SymbolKind.Variable,  g: 1 },
  ];

  const out: ExtractedSym[] = [];
  for (const { rx, kind, g } of rules) {
    rx.lastIndex = 0;
    let m: RegExpExecArray | null;
    while ((m = rx.exec(text))) {
      const name = m[g];
      if (name) out.push({ name, kind });
      if (m[0].length === 0) rx.lastIndex++;
    }
  }

  const seen = new Set<string>();
  return out.filter(s => {
    const k = `${s.kind}:${s.name}`;
    if (seen.has(k)) return false;
    seen.add(k);
    return true;
  });
}

function mapSymbolKindToCompletionKind(k: SymbolKind): CompletionItemKind {
  switch (k) {
    case SymbolKind.Function:     return CompletionItemKind.Function;
    case SymbolKind.Method:       return CompletionItemKind.Method;
    case SymbolKind.Struct:       return CompletionItemKind.Struct;
    case SymbolKind.Enum:         return CompletionItemKind.Enum;
    case SymbolKind.Interface:    return CompletionItemKind.Interface;
    case SymbolKind.Namespace:    return CompletionItemKind.Module;
    case SymbolKind.Class:        return CompletionItemKind.Class;
    case SymbolKind.Property:     return CompletionItemKind.Property;
    case SymbolKind.Field:        return CompletionItemKind.Field;
    case SymbolKind.Variable:     return CompletionItemKind.Variable;
    case SymbolKind.Constant:     return CompletionItemKind.Constant;
    case SymbolKind.Constructor:  return CompletionItemKind.Constructor;
    case SymbolKind.TypeParameter:return CompletionItemKind.TypeParameter;
    default:                      return CompletionItemKind.Text;
  }
}

/* ============================================================================
 * Utilitaires tokenisation et score
 * ========================================================================== */

function getLinePrefix(doc: TextDocument, pos: Position): string {
  return doc.getText(Range.create({ line: pos.line, character: 0 }, pos));
}

function currentToken(doc: TextDocument, pos: Position): { token: string; range: Range } {
  const line = doc.getText(Range.create({ line: pos.line, character: 0 }, { line: pos.line, character: 10_000 }));
  const idx = Math.min(pos.character, line.length);
  const left = line.slice(0, idx);
  const right = line.slice(idx);

  const leftMatch = /[A-Za-z_]\w*$/.exec(left);
  const rightMatch = /^\w*/.exec(right);

  const start = leftMatch ? idx - leftMatch[0].length : idx;
  const end = rightMatch ? idx + rightMatch[0].length : idx;

  const token = line.slice(start, end);
  return {
    token,
    range: Range.create({ line: pos.line, character: start }, { line: pos.line, character: end })
  };
}

function detectPositionContext(doc: TextDocument, pos: Position): CompletionPositionContext {
  const context = lexicalContextAt(doc.getText(), doc.offsetAt(pos));
  if (context !== "code") return context;

  const linePrefix = getLinePrefix(doc, pos);
  if (/^\s*use\s+/.test(linePrefix)) return "import";
  if (/:\s*[A-Za-z_0-9<>&[\]|?:.,\s]*$/.test(linePrefix)) return "type";
  if (/\b(?:as|is)\s+[A-Za-z_0-9<>&[\]|?:.,/\s]*$/.test(linePrefix)) return "type";
  return "code";
}

function fuzzyScore(candidate: string, query: string): number {
  if (!query) return 1;
  if (candidate.startsWith(query)) return 2; // boost prefix strict
  let qi = 0;
  for (let i = 0; i < candidate.length && qi < query.length; i++) {
    if (candidate[i].toLowerCase() === query[qi].toLowerCase()) qi++;
  }
  return 0.5 + qi / (2 * Math.max(1, query.length));
}

function extractScopedNames(doc: TextDocument, pos: Position): ScopedName[] {
  const text = doc.getText();
  const cursor = doc.offsetAt(pos);
  const head = text.slice(0, cursor);

  const out: ScopedName[] = [];
  const seen = new Set<string>();

  const letLike = /\b(?:let|const|share)\s+([A-Za-z_]\w*)/g;
  let m: RegExpExecArray | null;
  while ((m = letLike.exec(head))) {
    const name = m[1];
    if (seen.has(name)) continue;
    seen.add(name);
    out.push({
      name,
      kind: m[0].startsWith("const") ? SymbolKind.Constant : SymbolKind.Variable,
    });
  }

  const params = /\bproc\s+[A-Za-z_]\w*\s*\(([^)]*)\)/g;
  while ((m = params.exec(head))) {
    const plist = m[1];
    for (const raw of plist.split(",")) {
      const part = raw.trim();
      if (!part) continue;
      const pm = /^([A-Za-z_]\w*)/.exec(part);
      if (!pm) continue;
      const name = pm[1];
      if (seen.has(name)) continue;
      seen.add(name);
      out.push({ name, kind: SymbolKind.Variable });
    }
  }

  return out;
}

function extractDeclaredFunctions(doc: TextDocument): DeclFunction[] {
  const text = doc.getText();
  const rx = /\bproc\s+([A-Za-z_]\w*)\s*\(([^)]*)\)\s*(?:->\s*([A-Za-z_][\w<>\[\]|?:./]*))?/g;
  const out: DeclFunction[] = [];
  const seen = new Set<string>();
  let m: RegExpExecArray | null;
  while ((m = rx.exec(text))) {
    const name = m[1];
    if (seen.has(name)) continue;
    seen.add(name);
    out.push({
      name,
      params: (m[2] ?? "").trim(),
      returnType: m[3]?.trim() || undefined,
    });
    if (m[0].length === 0) rx.lastIndex++;
  }
  return out;
}

function addScopedCompletionItems(items: CompletionItem[], scoped: ScopedName[], token: string, range: Range): void {
  for (const s of scoped) {
    if (fuzzyScore(s.name, token) <= 0) continue;
    items.push({
      label: s.name,
      kind: mapSymbolKindToCompletionKind(s.kind),
      detail: "Scope local",
      documentation: md(`Nom local en scope: \`${s.name}\`.`),
      sortText: tier(0, `scope:${s.name}`),
      filterText: s.name,
      textEdit: edit(range, s.name),
      commitCharacters: DEFAULT_COMMIT_CHARS,
      labelDetails: { description: "local" },
      data: { source: "scope", name: s.name },
    });
  }
}

function addFunctionCompletionItems(items: CompletionItem[], funcs: DeclFunction[], token: string, range: Range): void {
  for (const fn of funcs) {
    if (fuzzyScore(fn.name, token) <= 0) continue;
    const signature = `${fn.name}(${fn.params})${fn.returnType ? ` -> ${fn.returnType}` : ""}`;
    items.push({
      label: fn.name,
      kind: CompletionItemKind.Function,
      detail: signature,
      documentation: md(`Fonction/proc déclarée: \`${signature}\`.`),
      sortText: tier(1, `filefn:${fn.name}`),
      filterText: fn.name,
      textEdit: edit(range, fn.name),
      insertText: `${fn.name}($0)`,
      insertTextFormat: InsertTextFormat.Snippet,
      commitCharacters: DEFAULT_COMMIT_CHARS,
      labelDetails: { description: "file function" },
      data: { source: "file-fn", signature },
    });
  }
}

function inferLocalTypes(doc: TextDocument, pos: Position): Map<string, InferredType> {
  const text = doc.getText().slice(0, doc.offsetAt(pos));
  const out = new Map<string, InferredType>();
  const rx = /\b(?:let|const|make)\s+([A-Za-z_]\w*)(?:\s*:\s*([A-Za-z_][\w<>[\]|?:./]*))?(?:\s*=\s*([^;\n]+))?/g;
  let m: RegExpExecArray | null;
  while ((m = rx.exec(text))) {
    const name = m[1];
    const explicitTy = m[2]?.trim();
    const value = (m[3] ?? "").trim();
    if (explicitTy) {
      out.set(name, explicitTy);
      continue;
    }
    if (/^(true|false)\b/.test(value)) out.set(name, "bool");
    else if (/^["']/.test(value)) out.set(name, "string");
    else if (/^-?\d+(\.\d+)?\b/.test(value)) out.set(name, "number");
    else out.set(name, "unknown");
    if (m[0].length === 0) rx.lastIndex++;
  }
  return out;
}

function nonNullVariablesInScope(doc: TextDocument, pos: Position): Set<string> {
  const text = doc.getText().slice(0, doc.offsetAt(pos));
  const out = new Set<string>();
  const checks = [
    /\bif\s+([A-Za-z_]\w*)\s+is\s+not\s+([A-Za-z_]\w*)\b/g,
  ];
  for (const rx of checks) {
    let m: RegExpExecArray | null;
    while ((m = rx.exec(text))) {
      out.add(m[1]);
      if (m[0].length === 0) rx.lastIndex++;
    }
  }
  return out;
}

function expectedTypeAtPosition(doc: TextDocument, pos: Position): InferredType {
  const prefix = getLinePrefix(doc, pos);
  if (/=\s*$/.test(prefix)) {
    const m = /\b(?:let|const|make)\s+[A-Za-z_]\w*\s*:\s*([A-Za-z_][\w<>[\]|?:./]*)\s*=\s*$/.exec(prefix);
    if (m?.[1]) return m[1].trim();
  }
  if (/\bif\s+[^\n]*$/.test(prefix)) return "bool";
  if (/:?\s*string\s*=\s*$/.test(prefix)) return "string";
  return "unknown";
}

function addExpectedValueCompletions(items: CompletionItem[], expected: InferredType, range: Range): void {
  if (expected === "bool") {
    for (const lit of ["true", "false"]) {
      items.push({
        label: lit,
        kind: CompletionItemKind.Value,
        detail: "Valeur attendue: bool",
        sortText: tier(0, `expected:${lit}`),
        filterText: lit,
        textEdit: edit(range, lit),
        commitCharacters: DEFAULT_COMMIT_CHARS,
        data: { source: "expected", expected: "bool" },
      });
    }
    return;
  }
  if (expected === "string") {
    items.push({
      label: "\"\"",
      kind: CompletionItemKind.Value,
      detail: "Valeur attendue: string",
      insertText: "\"$0\"",
      insertTextFormat: InsertTextFormat.Snippet,
      sortText: tier(0, "expected:string"),
      filterText: "\"\"",
      textEdit: edit(range, "\"\""),
      commitCharacters: DEFAULT_COMMIT_CHARS,
      data: { source: "expected", expected: "string" },
    });
  }
}

function addFlowSensitiveMemberCompletions(
  items: CompletionItem[],
  doc: TextDocument,
  pos: Position,
  localTypes: Map<string, InferredType>,
  nonNullVars: Set<string>,
): void {
  const linePrefix = getLinePrefix(doc, pos);
  const recvMatch = /([A-Za-z_]\w*)\.\s*$/.exec(linePrefix);
  if (!recvMatch) return;
  const recv = recvMatch[1];
  if (!nonNullVars.has(recv)) return;
  const ty = localTypes.get(recv) ?? "unknown";
  const members = ty === "string"
    ? ["len()", "trim()", "to_upper()", "to_lower()", "slice(${1:start}, ${2:end})"]
    : ty === "bool"
      ? ["to_string()"]
      : ["len()", "is_empty()", "to_string()"];
  for (const m of members) {
    items.push({
      label: m,
      kind: CompletionItemKind.Method,
      detail: `Flow-safe member (${recv} non-null)`,
      insertText: m,
      insertTextFormat: /[\$\{]/.test(m) ? InsertTextFormat.Snippet : InsertTextFormat.PlainText,
      sortText: tier(0, `flow:${recv}:${m}`),
      commitCharacters: DEFAULT_COMMIT_CHARS,
      data: { source: "flow", receiver: recv, inferredType: ty },
    });
  }
}

function addPostErrorRecoveryCompletions(items: CompletionItem[], doc: TextDocument, pos: Position): void {
  const text = doc.getText().slice(0, doc.offsetAt(pos));
  const opens = (text.match(/{/g) ?? []).length;
  const closes = (text.match(/}/g) ?? []).length;
  if (opens > closes) {
    items.push({
      label: "}",
      kind: CompletionItemKind.Text,
      detail: "Récupération syntaxe",
      documentation: md("Fermer le bloc courant."),
      sortText: tier(0, "recover:block-close"),
      insertText: "}",
      commitCharacters: DEFAULT_COMMIT_CHARS,
      data: { source: "recovery", kind: "brace" },
    });
  }
  const linePrefix = getLinePrefix(doc, pos);
  if (/^\s*(if|for|loop|match|when|otherwise|case)\b[^{\n]*$/.test(linePrefix)) {
    items.push({
      label: "{ ... }",
      kind: CompletionItemKind.Snippet,
      detail: "Récupération syntaxe",
      insertText: " {\n\t$0\n}",
      insertTextFormat: InsertTextFormat.Snippet,
      sortText: tier(0, "recover:block-body"),
      commitCharacters: DEFAULT_COMMIT_CHARS,
      data: { source: "recovery", kind: "body" },
    });
  }
}

function addRepoFrequentApiCompletions(items: CompletionItem[], token: string, range: Range): void {
  const frequentApis = [
    "len", "push", "pop", "clear", "iter",
    "println", "assert", "panic",
    "map", "filter", "reduce",
  ];
  for (const api of frequentApis) {
    if (fuzzyScore(api, token) <= 0) continue;
    items.push({
      label: api,
      kind: CompletionItemKind.Function,
      detail: "API fréquente (repo)",
      sortText: tier(2, `freq:${api}`),
      filterText: api,
      textEdit: edit(range, api),
      commitCharacters: DEFAULT_COMMIT_CHARS,
      data: { source: "repo-frequent", api },
    });
  }
}

function hasPathImport(doc: TextDocument, pathValue: string): boolean {
  const text = doc.getText();
  const lineRx = new RegExp(`^\\s*(?:use|pull)\\s+${escapeRegex(pathValue)}\\b`, "m");
  return lineRx.test(text);
}

function buildAutoImportEdit(doc: TextDocument, pathValue: string): LspTextEdit[] | undefined {
  if (!pathValue || hasPathImport(doc, pathValue)) return undefined;
  const lines = doc.getText().split(/\r?\n/);
  let insertLine = 0;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i] ?? "";
    if (/^\s*space\b/.test(line)) {
      insertLine = i + 1;
      continue;
    }
    if (/^\s*(use|pull)\b/.test(line)) {
      insertLine = i + 1;
      continue;
    }
    if (line.trim() !== "") break;
  }
  return [{
    range: Range.create(
      { line: insertLine, character: 0 },
      { line: insertLine, character: 0 }
    ),
    newText: `use ${pathValue}\n`,
  }];
}

/* ============================================================================
 * Complétions contextuelles
 * ========================================================================== */

function diagnosticsCompletion(linePrefix: string): CompletionItem[] {
  const items: CompletionItem[] = [];
  if (linePrefix.includes("???")) {
    items.push({
      label: "TODO",
      kind: CompletionItemKind.Text,
      insertText: "TODO",
      detail: "Remplacer ??? par TODO",
      documentation: md("Transforme la séquence `???` en TODO/FIXME."),
      sortText: tier(0)
    });
  }
  if (/^\s*(?:use|pull)\s+/.test(linePrefix)) {
    for (const imp of VITTE_PACKAGE_IMPORTS) {
      items.push({
        label: imp,
        kind: CompletionItemKind.Module,
        detail: "Import rapide",
        documentation: md(`Ajoute l'import \`${imp}\`.`),
        sortText: tier(3, imp),
        filterText: imp,
        commitCharacters: DEFAULT_COMMIT_CHARS,
        data: { source: "stdlib-import", modulePath: imp }
      });
    }
  }
  return items;
}

function memberCompletion(leftOfCursor: string): CompletionItem[] {
  const items: CompletionItem[] = [];
  const memberCtx = /([A-Za-z_]\w*)\.\s*$/.exec(leftOfCursor);
  if (!memberCtx) return items;
  const recv = memberCtx[1];

  for (const group of COMMON_MEMBERS) {
    if (group.recv.test(recv)) {
      for (const m of group.members) {
        items.push({
          label: m,
          kind: CompletionItemKind.Method,
          detail: `Méthode de ${recv}`,
          documentation: md(`Proposition basée sur le nom \`${recv}\`.`),
          sortText: tier(0, m),
          insertText: m,
          insertTextFormat: /[\$\{]/.test(m) ? InsertTextFormat.Snippet : InsertTextFormat.PlainText
        });
      }
    }
  }
  // Fallback générique
  if (items.length === 0) {
    for (const m of ["len()", "is_empty()", "to_string()", "clone()", "dbg()"]) {
      items.push({
        label: m,
        kind: CompletionItemKind.Method,
        detail: `Méthode`,
        documentation: md("Proposition générique."),
        sortText: tier(1, m),
        insertText: m,
        insertTextFormat: InsertTextFormat.PlainText
      });
    }
  }
  return items;
}

function workspaceSymbolCompletions(
  doc: TextDocument,
  token: string,
  range: Range,
  context: "import" | "general"
): CompletionItem[] {
  const normalized = token.trim();
  if (!normalized && context === "general") return [];
  const limit = context === "import" ? 200 : 80;
  const results = searchWorkspaceSymbols(normalized, limit);
  if (results.length === 0) return [];

  const items: CompletionItem[] = [];
  const seen = new Set<string>();

  for (const sym of results) {
    if (!sym?.name) continue;
    if (sym.uri === doc.uri) continue;
    if (context === "general" && fuzzyScore(sym.name, token) <= 0) continue;
    if (context === "import" && !isImportableKind(sym.kind)) continue;

    const key = `${sym.name}|${sym.uri}|${sym.kind}`;
    if (seen.has(key)) continue;
    seen.add(key);

    const kind = mapSymbolKindToCompletionKind(sym.kind as unknown as SymbolKind);
    const detailParts = [`${SYMBOL_KIND_LABEL[sym.kind] ?? "Symbole"}`];
    if (sym.uri) {
      const base = basenameFromUri(sym.uri);
      if (base) detailParts.push(base);
    }

    items.push({
      label: sym.name,
      kind,
      detail: detailParts.join(" — "),
      documentation: md(`Symbole workspace \`${sym.name}\`.`),
      sortText: tier(context === "import" ? 2 : 2, `ws:${sym.name}`),
      filterText: sym.name,
      textEdit: edit(range, sym.name),
      additionalTextEdits: context === "general" ? buildAutoImportEdit(doc, sym.name) : undefined,
      commitCharacters: DEFAULT_COMMIT_CHARS,
      labelDetails: context === "import"
        ? { detail: "workspace" }
        : { description: SYMBOL_KIND_LABEL[sym.kind] ?? "project symbol" },
      data: {
        source: "workspace",
        modulePath: sym.name,
        uri: sym.uri,
      },
    });
    if (context === "general" && items.length >= 40) break;
    if (context === "import" && items.length >= 60) break;
  }

  return items;
}

function addLocalImportPathCompletions(
  items: CompletionItem[],
  doc: TextDocument,
  token: string,
  range: Range,
): void {
  const paths = extractDocumentModulePaths(doc);
  for (const modulePath of paths) {
    if (token && fuzzyScore(modulePath, token) <= 0) continue;
    items.push({
      label: modulePath,
      kind: CompletionItemKind.Module,
      detail: "Chemin du fichier courant",
      documentation: md(`Chemin déjà déclaré ou importé: \`${modulePath}\`.`),
      sortText: tier(1, `local-import:${modulePath}`),
      filterText: modulePath,
      textEdit: edit(range, modulePath),
      commitCharacters: DEFAULT_COMMIT_CHARS,
      labelDetails: { description: "local import" },
      data: { source: "local-import", modulePath },
    });
  }
}

function extractDocumentModulePaths(doc: TextDocument): string[] {
  const text = doc.getText();
  const out = new Set<string>();
  const add = (value: string | undefined) => {
    const normalized = (value ?? "").trim().replace(/;$/, "");
    if (normalized.length > 0) out.add(normalized);
  };
  for (const match of text.matchAll(/^\s*space\s+([A-Za-z_][\w./:-]*)/gm)) {
    add(match[1]);
  }
  for (const match of text.matchAll(/^\s*(?:pull|use)\s+([A-Za-z_][\w./:-]*)/gm)) {
    add(match[1]);
  }
  return [...out].sort((a, b) => a.localeCompare(b));
}

function isImportableKind(kind: SK): boolean {
  return kind === SK.Module || kind === SK.Namespace;
}

function basenameFromUri(uri: string): string {
  if (!uri) return "";
  try {
    if (uri.startsWith("file://")) {
      return path.basename(fileURLToPath(uri));
    }
  } catch {
    // ignore decoding issues
  }
  try {
    return path.basename(new URL(uri).pathname);
  } catch {
    return uri;
  }
}

/* ============================================================================
 * API
 * ========================================================================== */

export function provideCompletions(doc: TextDocument, position: Position): CompletionItem[] {
  const items: CompletionItem[] = [];
  const { token, range } = currentToken(doc, position);
  const linePrefix = getLinePrefix(doc, position);
  const context = detectPositionContext(doc, position);
  const localScoped = extractScopedNames(doc, position);
  const declaredFunctions = extractDeclaredFunctions(doc);
  const localTypes = inferLocalTypes(doc, position);
  const nonNullVars = nonNullVariablesInScope(doc, position);
  const expectedType = expectedTypeAtPosition(doc, position);

  if (context === "comment" && !completionSettings.enabledInComments) return [];
  if (context === "string" && !completionSettings.enabledInStrings) return [];

  // Mots-clés
  for (const kw of KEYWORDS) {
    const score = fuzzyScore(kw, token);
    if (score > 0) {
      items.push({
        label: kw,
        kind: CompletionItemKind.Keyword,
        detail: "Mot-clé",
        documentation: md(`Mot-clé du langage \`${kw}\`.`),
        sortText: tier(2 - (score >= 2 ? 1 : 0), kw),
        filterText: kw,
        textEdit: edit(range, kw),
        preselect: kw === "proc" || kw === "let",
        commitCharacters: DEFAULT_COMMIT_CHARS,
        labelDetails: { description: "keyword" }
      });
    }
  }

  // Littéraux
  for (const lit of [...BOOL_LITERALS, ...NIL_LITERALS]) {
    if (fuzzyScore(lit, token) > 0) {
      items.push({
        label: lit,
        kind: CompletionItemKind.Value,
        detail: "Littéral",
        documentation: md(`Littéral \`${lit}\`.`),
        sortText: tier(2, `~${lit}`),
        textEdit: edit(range, lit),
        commitCharacters: DEFAULT_COMMIT_CHARS,
        labelDetails: { description: "literal" }
      });
    }
  }

  // Types primitifs
  for (const ty of PRIMITIVE_TYPES) {
    if (fuzzyScore(ty, token) > 0) {
      items.push({
        label: ty,
        kind: CompletionItemKind.TypeParameter,
        detail: "Type primitif",
        documentation: md(`Type primitif \`${ty}\`.`),
        sortText: tier(2, `type:${ty}`),
        textEdit: edit(range, ty),
        commitCharacters: DEFAULT_COMMIT_CHARS,
        labelDetails: { description: "type" }
      });
    }
  }

  // Fonctions builtin
  for (const fn of BUILTIN_FUNCTIONS) {
    if (fuzzyScore(fn, token) > 0) {
      items.push({
        label: fn,
        kind: CompletionItemKind.Function,
        detail: "Builtin",
        documentation: md(`Fonction builtin \`${fn}\`.`),
        sortText: tier(2, `builtin:${fn}`),
        textEdit: edit(range, fn),
        commitCharacters: DEFAULT_COMMIT_CHARS,
        labelDetails: { description: "builtin" }
      });
    }
  }

  // Scope local (priorité max)
  addScopedCompletionItems(items, localScoped, token, range);

  // Fonctions déclarées dans le fichier
  addFunctionCompletionItems(items, declaredFunctions, token, range);

  // Valeurs attendues (bool/string/minimal typing)
  addExpectedValueCompletions(items, expectedType, range);

  // APIs fréquentes observées dans le repo
  addRepoFrequentApiCompletions(items, token, range);

  // Snippets
  if (completionSettings.includeSnippets && context !== "string" && context !== "comment") {
    for (const it of SNIPPETS) {
      items.push({
        ...it,
        sortText: tier(4, String(it.label)),
        filterText: String(it.label),
        commitCharacters: DEFAULT_COMMIT_CHARS,
        data: { source: "snippet", label: it.label }
      });
    }
  }

  // Symboles du document
  for (const s of extractSymbols(doc)) {
    const label = s.name;
    if (fuzzyScore(label, token) > 0) {
      items.push({
        label,
        kind: mapSymbolKindToCompletionKind(s.kind),
        detail: `Symbole (${SYMBOL_KIND_LABEL[s.kind] ?? "?"})`,
        documentation: md("Déclaré dans ce document."),
        sortText: tier(1, label),
        filterText: label,
        textEdit: edit(range, label),
        commitCharacters: DEFAULT_COMMIT_CHARS,
        labelDetails: { description: SYMBOL_KIND_LABEL[s.kind] ?? "symbol" }
      });
    }
  }

  // Contexte: membres après un point
  if (context !== "type") {
    items.push(...memberCompletion(linePrefix));
    addFlowSensitiveMemberCompletions(items, doc, position, localTypes, nonNullVars);
  }

  // Contexte: import et autres heuristiques
  const isImportContext = /^\s*(?:use|pull)\s+/.test(linePrefix);
  if (isImportContext) {
    addLocalImportPathCompletions(items, doc, token, range);
  }

  items.push(...diagnosticsCompletion(linePrefix).map(ci => ({
    ...ci,
    textEdit: ci.insertText ? undefined : edit(range, ci.label),
    sortText: ci.sortText ?? tier(3, ci.label),
    commitCharacters: ci.commitCharacters ?? DEFAULT_COMMIT_CHARS,
  })));

  // Suggestions de récupération quand le code est incomplet/cassé.
  addPostErrorRecoveryCompletions(items, doc, position);

  if (completionSettings.workspaceSymbols) {
    if (isImportContext) {
      items.push(...workspaceSymbolCompletions(doc, token, range, "import"));
    } else if (token.length >= 2) {
      items.push(...workspaceSymbolCompletions(doc, token, range, "general"));
    }
  }

  return dedupe(items, it => `${it.label}|${it.kind}|${it.sortText ?? ""}`)
    .slice(0, completionSettings.maxItems);
}

export function resolveCompletion(item: CompletionItem): CompletionItem {
  const data = (item.data && typeof item.data === "object") ? item.data as Record<string, unknown> : undefined;
  if (!item.documentation && typeof item.label === "string") {
    item.documentation = md(`Entrée de complétion \`${item.label}\`.`);
  }
  if (data?.source === "workspace" && typeof data.modulePath === "string") {
    item.documentation = md(
      `Symbole projet: \`${data.modulePath}\`\n\n` +
      `Import auto: \`use ${data.modulePath}\` (si absent).`
    );
  } else if (data?.source === "file-fn" && typeof data.signature === "string") {
    item.documentation = md(`Signature: \`${data.signature}\``);
  } else if (data?.source === "scope" && typeof data.name === "string") {
    item.documentation = md(`Variable/const locale: \`${data.name}\`.`);
  } else if (data?.source === "stdlib-import" && typeof data.modulePath === "string") {
    item.documentation = md(`Module stdlib: \`${data.modulePath}\``);
  }
  return item;
}

export function triggerCharacters(): string[] {
  const letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_".split("");
  return [".", ":", ">", "=", " ", "(", "[", "{", "\"", "'", ...letters];
}

/* ============================================================================
 * Helpers
 * ========================================================================== */

function ciSnippet(label: string, detail: string, documentation: string, insertText: string): CompletionItem {
  return {
    label,
    kind: CompletionItemKind.Snippet,
    insertText,
    insertTextFormat: InsertTextFormat.Snippet,
    detail,
    documentation: md(documentation)
  };
}

function tier(n: number, suffix = ""): string {
  // 0 = top, 1 = symboles, 2 = mots-clés/littéraux, 3 = snippets
  return `${n.toString(10).padStart(2, "0")}_${suffix}`;
}

function md(value: string): MarkupContent {
  return { kind: MarkupKind.Markdown, value };
}

function edit(range: Range, newText: string): TextEdit {
  return { range, newText };
}

function dedupe<T>(arr: T[], key: (t: T) => string): T[] {
  const seen = new Set<string>();
  const out: T[] = [];
  for (const el of arr) {
    const k = key(el);
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(el);
  }
  return out;
}

const DEFAULT_COMMIT_CHARS = [" ", "\t", "(", ")", "{", "}", "[", "]", ";", ",", "."];

function escapeRegex(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function clampNumber(value: number | undefined, min: number, max: number, fallback: number): number {
  if (typeof value !== "number" || !Number.isFinite(value)) return fallback;
  return Math.min(max, Math.max(min, Math.floor(value)));
}
