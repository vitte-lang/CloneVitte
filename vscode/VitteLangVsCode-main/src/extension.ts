import { workspaceBinaryRoots, expandToolPath } from "./utils/project";
import { normalizeCompilerDiagnostics } from "./utils/compilerDiagnostics";
/*
 * extension.ts — VS Code client for Vitte/Vit
 * - Full LSP client (start/stop/restart, traces, status, watchers)
 * - Commands: logs, restart, actions (format/organize/fix), rename, debug demos
 * - Progress UI, output channel, status bar, config-sync, file watcher
 * - Hardened: no-op when no editor, consistent error handling, strict types
 */

import * as path from "node:path";
import * as fs from "node:fs";
import * as os from "node:os";
import * as cp from "node:child_process";
import * as vscode from "vscode";
import { PlaygroundPanel } from "./providers/playgroundPanel";
import { registerBuildTasks } from "./tasks/buildTasks";
import { registerBenchTasks } from "./tasks/benchTasks";
import { registerRuntimeLocatorCommand } from "./debug/runtimeLocator";
import { registerDebugFactory } from "./debug/adapterFactory";
import { registerDebugConfigurationProvider } from "./debug/configurationProvider";
import { registerTelemetry } from "./utils/telemetry";
import { registerQuickActions } from "./commands/quickActions";
import { registerModuleGraphView } from "./providers/moduleGraphView";
import { registerProjectAssistant } from "./commands/projectAssistant";
import { registerAdvancedCodeActions } from "./providers/advancedCodeActions";
import { registerVitteCodeLens } from "./providers/vitteCodeLens";
import { registerEnterpriseSuite } from "./commands/enterpriseSuite";
import { registerCommandCenterView } from "./providers/commandCenterView";
import { registerDiagnosticsView } from "./diagnosticsView";
import { registerPackageProblemsView } from "./providers/packageProblemsView";
import { registerTopSyntaxErrorsView } from "./providers/topSyntaxErrorsView";
import {
  LanguageClient,
  TransportKind,
  RevealOutputChannelOn,
  State as ClientState,
} from "vscode-languageclient/node";
import type {
  LanguageClientOptions,
  ServerOptions,
  DocumentSelector,
  ProvideDocumentFormattingEditsSignature,
} from "vscode-languageclient/node";
import {
  summarizeWorkspaceDiagnostics,
  buildWorkspaceDiagnosticsReport,
  summarizeDiagnosticsByDirectory,
  diagnosticsLevel,
  formatDiagnosticsSummary,
} from "./utils/diagnostics";
import type { ServerMetricEntry } from "./types/metrics";
import {
  rankAndTrimCompletionItems,
  recordSuggestionUsage,
  recordSuggestionRejections,
  updateSuggestionChurnForRequest,
  resetSuggestionLearningState,
  getSuggestionLearningSnapshot,
  type SuggestionTuning,
  buildLocalCompletionBatch,
  buildPrefixOnlyLocalBatch,
  getCompletionRequestKey,
  getCompletionContextKey,
  getCompletionDocumentKey,
} from "./utils/suggestions";
import { LocalInlineEngine, type LocalInlineEngineOptions } from "./utils/localInlineEngine";
import { AiInlinePipeline, type AiInlinePipelineOptions } from "./utils/aiInlinePipeline";

let client: LanguageClient | undefined;
let output: vscode.OutputChannel;
let statusItem: vscode.StatusBarItem;

let statusBaseIcon = "$(rocket)";
const STATUS_LABEL = "Vitte";
let statusLanguageSuffix = "";
let statusBaseTooltip = "Vitte Language Server";
let statusHealthIcon = "";
let statusHealthTooltip = "";
let statusOverrideText: string | undefined;
let statusOverrideTooltip: string | undefined;
let offlineReason: string | undefined;
let offlineBannerShown = false;
const recentStops: number[] = [];
const OFFLINE_STATUS_COLOR = new vscode.ThemeColor("statusBarItem.warningForeground");
const OFFLINE_STATUS_BG = new vscode.ThemeColor("statusBarItem.warningBackground");
let offlineRetryTimer: NodeJS.Timeout | undefined;
let offlineRetryMs = 60000;
let lastActivationContext: vscode.ExtensionContext | undefined;
let editorLintCollection: vscode.DiagnosticCollection | undefined;
let syntaxLintCollection: vscode.DiagnosticCollection | undefined;
let offlineSince: number | undefined;
const formatOnSaveInFlight = new Set<string>();
const syntaxLintTimers = new Map<string, NodeJS.Timeout>();
const syntaxLintSeq = new Map<string, number>();
const syntaxLintProcByDoc = new Map<string, cp.ChildProcess>();
let syntaxParsingInFlight = 0;
let healthCheckTimer: NodeJS.Timeout | undefined;
let healthFailures = 0;
let healthRestartInFlight = false;
let reliabilityAttempts = 0;
let reliabilityNextDelayMs = 30000;
let restartInFlight: Promise<boolean> | undefined;
let restartQueued = false;
let restartQueueReason = "";
let configRestartTimer: NodeJS.Timeout | undefined;
let extensionShuttingDown = false;
let activationStartedAt = Date.now();
const completionLatencyWindowMs: number[] = [];
const completionStreamingCachePrefix = new Map<string, { items: vscode.CompletionItem[]; ts: number }>();
const completionStreamingCacheContext = new Map<string, { items: vscode.CompletionItem[]; ts: number }>();
const completionStreamingCacheDocument = new Map<string, { items: vscode.CompletionItem[]; ts: number }>();
const completionStreamingInFlight = new Map<string, Promise<void>>();
const completionPagingState = new Map<string, { page: number; ts: number }>();
const completionAstFingerprintByDoc = new Map<string, string>();
const completionAstRefreshTimers = new Map<string, NodeJS.Timeout>();
let completionStreamingRefreshGuard = false;
let completionNextInvokeIsAutoRefresh = false;
let completionLoadNextPageKey: string | undefined;
let completionLoadNextPageIssuedAt = 0;
const completionTop1StableByKey = new Map<string, { text: string; count: number; lastAt: number }>();
const inlineCompletionCache = new Map<string, { items: vscode.InlineCompletionItem[]; ts: number }>();
const inlineNextEditBoostByDoc = new Map<string, number>();
const inlineSuggestionCycleByKey = new Map<string, number>();
const inlineTopCandidateByDoc = new Map<string, { line: number; character: number; left: string; text: string; at: number }>();
const INLINE_CACHE_TTL_MS = 150;
let localInlineEngine: LocalInlineEngine | undefined;
let aiInlinePipeline: AiInlinePipeline | undefined;
const localInlineReindexTimers = new Map<string, NodeJS.Timeout>();
let suggestionEngineModeSuffix = "local";
let completionIdlePrefetchTimer: NodeJS.Timeout | undefined;
let completionPrefetchInFlight = false;
let completionPrefetchExpectedKey: string | undefined;
const typingSpeedByDoc = new Map<string, { lastEditAt: number; intervalEwmaMs: number }>();
const completionRequestStartedAt = new Map<string, number>();
const completionRequestRefreshed = new Map<string, boolean>();
const completionFirstUsableMarked = new Set<string>();
const completionLspNegativeCache = new Map<string, { timeouts: number; until: number }>();
const completionStreamingFirstPaintMs: number[] = [];
const completionStreamingEnrichMs: number[] = [];
const completionFirstUsableMs: number[] = [];
let completionStreamingRefreshCount = 0;
let completionStreamingTimeoutCount = 0;
let completionStreamingTotalCount = 0;
let completionStreamingManualRefreshCount = 0;
let completionStreamingRicherRefreshCount = 0;
let completionStreamingIncompleteRefreshCount = 0;
let completionSuggestionShownCount = 0;
let completionSuggestionAcceptedCount = 0;
let completionSuggestionCanceledCount = 0;
let completionSuggestionPendingAccepted = false;
let completionPendingRejectedItems: vscode.CompletionItem[] = [];
const completionShownBySource = new Map<string, number>();
const completionAcceptedBySource = new Map<string, number>();
const completionStickyAcceptedByDoc = new Map<string, string[]>();
let completionCacheRequestCount = 0;
let completionCacheHitCount = 0;
let completionPrefetchRequestCount = 0;
let completionPrefetchHitCount = 0;
let completionColdStartCount = 0;
const completionColdStartMs: number[] = [];
const completionStableListMs: number[] = [];
const completionNoRefreshStrictMs: number[] = [];
const completionShadowAgreement: number[] = [];
const completionShadowDrift: number[] = [];
const completionOpenedAtByDoc = new Map<string, number>();
let completionCacheEvictionCount = 0;
let completionCachePressureEvents = 0;
let lastCachePressureLogAt = 0;
let completionFallbackTimeoutCount = 0;
let completionFallbackNegativeCacheCount = 0;
let completionFallbackOfflineCount = 0;
let completionFallbackCancelCount = 0;
let lastDiagSchemaSignature = "";
interface DiagnosticExplainPayload {
  help: string;
  example?: string;
  source?: "external" | "local";
}
interface DiagnosticHelpObservabilitySnapshot {
  requests: number;
  explainResolved: number;
  explainThrottled: number;
  localFallbackResolved: number;
  localOnlyResolved: number;
  unresolved: number;
  explainUsageRate: number;
  explainThrottledRate: number;
  localFallbackRate: number;
  localOnlyRate: number;
}
const vitteExplainHelpCache = new Map<string, { help: string; example?: string; at: number }>();
const vitteExplainHelpCacheLoadedFiles = new Set<string>();
const vitteExplainHelpCachePersistTimers = new Map<string, NodeJS.Timeout>();
const vitteVersionSignatureByBin = new Map<string, string>();
let diagnosticHelpRequests = 0;
let diagnosticHelpExplainResolved = 0;
let diagnosticHelpExplainThrottled = 0;
let diagnosticHelpLocalFallbackResolved = 0;
let diagnosticHelpLocalOnlyResolved = 0;
let diagnosticHelpUnresolved = 0;
const EXPLAIN_THROTTLE_WINDOW_MS = 1000;
const EXPLAIN_THROTTLE_MAX_CALLS = 3;
const explainCallTimestamps: number[] = [];
type SuggestionTraceRefreshCause = "none" | "incomplete" | "richer";
type SuggestionTraceFallbackCause = "none" | "timeout" | "negative_cache" | "offline" | "cancel";
interface SuggestionTraceEntry {
  id: string;
  requestKey: string;
  languageId: string;
  uriShort: string;
  startedAt: number;
  firstPaintMs?: number;
  enrichMs?: number;
  firstUsableMs?: number;
  stableMs?: number;
  noRefreshStrict?: boolean;
  refreshCause: SuggestionTraceRefreshCause;
  fallbackCause: SuggestionTraceFallbackCause;
  shownCount: number;
  sourceMix?: string;
}
const completionTraceActiveByKey = new Map<string, SuggestionTraceEntry>();
const completionTraceHistory: SuggestionTraceEntry[] = [];
let suggestionProfilerPanel: vscode.WebviewPanel | undefined;
let suggestionProfilerRenderTimer: NodeJS.Timeout | undefined;
let lastSettingsIssueDigest = "";

function incCounter(map: Map<string, number>, key: string, delta = 1): void {
  map.set(key, (map.get(key) ?? 0) + delta);
}

function extractItemSource(item: vscode.CompletionItem): string {
  const detail = Array.isArray(item.detail) ? item.detail.join(" ") : (item.detail ?? "");
  const m = /\[source:([a-z]+)\]/i.exec(detail);
  return (m?.[1] ?? "unknown").toLowerCase();
}

function normalizeCompletionLabel(item: vscode.CompletionItem): string {
  const label = typeof item.label === "string" ? item.label : item.label.label;
  return label.trim().toLowerCase();
}

function extractCompletionInsertText(item: vscode.CompletionItem): string {
  if (typeof item.insertText === "string") return item.insertText;
  if (item.insertText instanceof vscode.SnippetString) return item.insertText.value;
  const label = typeof item.label === "string" ? item.label : item.label.label;
  return label;
}

function recordStickyAcceptedLabel(documentUri: string, item: vscode.CompletionItem, limit: number): void {
  const key = normalizeCompletionLabel(item);
  const prev = completionStickyAcceptedByDoc.get(documentUri) ?? [];
  const next = [key, ...prev.filter((v) => v !== key)].slice(0, Math.max(1, limit));
  completionStickyAcceptedByDoc.set(documentUri, next);
}

function withSourceBadge(
  item: vscode.CompletionItem,
  source: "local" | "lsp" | "plugin" | "cache",
  force = false,
): vscode.CompletionItem {
  const detail = Array.isArray(item.detail) ? item.detail.join(" ") : (item.detail ?? "");
  if (force) {
    const stripped = detail.replace(/\[source:[a-z]+\]\s*/ig, "").trim();
    item.detail = stripped ? `[source:${source}] ${stripped}` : `[source:${source}]`;
    return item;
  }
  if (/\[source:[a-z]+\]/i.test(detail)) return item;
  item.detail = detail ? `[source:${source}] ${detail}` : `[source:${source}]`;
  return item;
}

function shouldKeepSuggestionWithLintGuard(
  item: vscode.CompletionItem,
  document: vscode.TextDocument,
  position: vscode.Position,
  enabled: boolean,
): boolean {
  if (!enabled) return true;
  const diagnostics = vscode.languages.getDiagnostics(document.uri);
  const hasLineError = diagnostics.some((d) => d.severity === vscode.DiagnosticSeverity.Error && d.range.start.line === position.line);
  if (!hasLineError) return true;
  const range = document.getWordRangeAtPosition(position);
  const prefix = range ? document.getText(range).toLowerCase() : "";
  if (item.kind === vscode.CompletionItemKind.Snippet) return false;
  const label = typeof item.label === "string" ? item.label : item.label.label;
  const insert = typeof item.insertText === "string"
    ? item.insertText
    : item.insertText instanceof vscode.SnippetString
      ? item.insertText.value
      : label;
  const normalized = insert.replace(/\s+/g, " ").trim().toLowerCase();
  if (normalized.includes("\n")) return false;
  if (prefix.length === 0) return true;
  return normalized.startsWith(prefix) || normalized.includes(prefix);
}

function suggestionText(item: vscode.CompletionItem): string {
  const label = typeof item.label === "string" ? item.label : item.label.label;
  const insert = typeof item.insertText === "string"
    ? item.insertText
    : item.insertText instanceof vscode.SnippetString
      ? item.insertText.value
      : label;
  return `${label} ${insert}`.toLowerCase();
}

function shouldKeepSuggestionWithSecurityGuard(
  item: vscode.CompletionItem,
  enabled: boolean,
  blockedPatterns: string[],
): boolean {
  if (!enabled) return true;
  const text = suggestionText(item);
  for (const pattern of blockedPatterns) {
    if (!pattern || pattern.trim().length === 0) continue;
    try {
      const re = new RegExp(pattern, "i");
      if (re.test(text)) return false;
    } catch {
      // Ignore invalid regex patterns from config.
    }
  }
  return true;
}

function shouldKeepSuggestionWithPatternLists(
  item: vscode.CompletionItem,
  whitelistPatterns: string[],
  blacklistPatterns: string[],
): boolean {
  const text = suggestionText(item);
  let whitelisted = whitelistPatterns.length === 0;
  for (const pattern of whitelistPatterns) {
    if (!pattern || pattern.trim().length === 0) continue;
    try {
      const re = new RegExp(pattern, "i");
      if (re.test(text)) {
        whitelisted = true;
        break;
      }
    } catch {
      // Ignore invalid regex from user config.
    }
  }
  if (!whitelisted) return false;

  for (const pattern of blacklistPatterns) {
    if (!pattern || pattern.trim().length === 0) continue;
    try {
      const re = new RegExp(pattern, "i");
      if (re.test(text)) return false;
    } catch {
      // Ignore invalid regex from user config.
    }
  }
  return true;
}

function shouldKeepSuggestionWithDiagnosticContext(
  item: vscode.CompletionItem,
  document: vscode.TextDocument,
  position: vscode.Position,
  enabled: boolean,
  minTokenLen: number,
): boolean {
  if (!enabled) return true;
  const diagnostics = vscode.languages.getDiagnostics(document.uri)
    .filter((d) => d.range.start.line === position.line);
  if (diagnostics.length === 0) return true;

  const text = suggestionText(item);
  const wordRange = document.getWordRangeAtPosition(position);
  const prefix = wordRange ? document.getText(wordRange).toLowerCase() : "";
  if (prefix && text.includes(prefix)) return true;

  const tokens = new Set<string>();
  for (const d of diagnostics) {
    const parts = d.message.toLowerCase().split(/[^a-z0-9_]+/g);
    for (const t of parts) {
      if (t.length >= minTokenLen) tokens.add(t);
    }
  }
  if (tokens.size === 0) return true;

  for (const t of tokens) {
    if (text.includes(t)) return true;
  }
  return false;
}

function mapEstimatedMb(map: Map<string, { items: vscode.CompletionItem[]; ts: number }>): number {
  let bytes = 0;
  for (const [k, v] of map) {
    bytes += k.length * 2;
    bytes += 16;
    for (const it of v.items) {
      const lbl = typeof it.label === "string" ? it.label : it.label.label;
      bytes += lbl.length * 2 + 64;
    }
  }
  return bytes / (1024 * 1024);
}

function touchMapEntry<K, V>(map: Map<K, V>, key: K, value: V): void {
  if (map.has(key)) map.delete(key);
  map.set(key, value);
}

function evictLruEntries<K, V>(map: Map<K, V>, maxEntries: number): number {
  let evicted = 0;
  while (map.size > maxEntries) {
    const oldest = map.keys().next().value;
    if (oldest === undefined) break;
    map.delete(oldest);
    evicted += 1;
  }
  return evicted;
}

function trimCompletionCaches(maxEntries: number, softMemoryMb: number): void {
  const beforePrefix = completionStreamingCachePrefix.size;
  const beforeContext = completionStreamingCacheContext.size;
  const beforeDocument = completionStreamingCacheDocument.size;
  completionCacheEvictionCount += evictLruEntries(completionStreamingCachePrefix, maxEntries);
  completionCacheEvictionCount += evictLruEntries(completionStreamingCacheContext, maxEntries);
  completionCacheEvictionCount += evictLruEntries(completionStreamingCacheDocument, maxEntries);

  const rssMb = process.memoryUsage().rss / (1024 * 1024);
  const cacheMb = mapEstimatedMb(completionStreamingCachePrefix)
    + mapEstimatedMb(completionStreamingCacheContext)
    + mapEstimatedMb(completionStreamingCacheDocument);
  if (rssMb <= softMemoryMb && cacheMb <= Math.max(8, softMemoryMb * 0.25)) return;

  completionCachePressureEvents += 1;
  let target = Math.max(32, Math.floor(maxEntries * 0.8));
  if (rssMb > softMemoryMb * 1.5) {
    target = Math.max(32, Math.floor(maxEntries * 0.6));
  }
  if (rssMb > softMemoryMb * 2.0) {
    target = Math.max(16, Math.floor(maxEntries * 0.35));
  }
  completionCacheEvictionCount += evictLruEntries(completionStreamingCachePrefix, target);
  completionCacheEvictionCount += evictLruEntries(completionStreamingCacheContext, target);
  completionCacheEvictionCount += evictLruEntries(completionStreamingCacheDocument, target);

  const evictedTotal = (beforePrefix - completionStreamingCachePrefix.size)
    + (beforeContext - completionStreamingCacheContext.size)
    + (beforeDocument - completionStreamingCacheDocument.size);
  if (evictedTotal > 0 && (Date.now() - lastCachePressureLogAt) > 30000) {
    lastCachePressureLogAt = Date.now();
    obsLog("completion.cache.pressure.trim", "warn", {
      rssMb: Number(rssMb.toFixed(1)),
      cacheMb: Number(cacheMb.toFixed(1)),
      maxEntries,
      target,
      evictedTotal,
      pressureEvents: completionCachePressureEvents,
    });
  }
}

function getValidCacheItems(
  map: Map<string, { items: vscode.CompletionItem[]; ts: number }>,
  key: string,
  ttlMs: number,
): vscode.CompletionItem[] {
  const now = Date.now();
  const entry = map.get(key);
  if (!entry) return [];
  if ((now - entry.ts) > ttlMs) {
    map.delete(key);
    return [];
  }
  // touch for LRU behavior
  map.delete(key);
  map.set(key, entry);
  return entry.items;
}

function clearCacheMapByUriPrefix(
  map: Map<string, unknown>,
  uriPrefix: string,
): void {
  for (const key of map.keys()) {
    if (key.startsWith(uriPrefix)) map.delete(key);
  }
}

function clearSetByUriPrefix(
  set: Set<string>,
  uriPrefix: string,
): void {
  for (const key of set.values()) {
    if (key.startsWith(uriPrefix)) set.delete(key);
  }
}

function invalidateCompletionCachesForDocument(document: vscode.TextDocument): void {
  const uriPrefix = `${document.uri.toString()}#`;
  const inlinePrefix = `${document.uri.toString()}::`;
  clearCacheMapByUriPrefix(completionStreamingCachePrefix, uriPrefix);
  clearCacheMapByUriPrefix(completionStreamingCacheContext, uriPrefix);
  clearCacheMapByUriPrefix(completionStreamingCacheDocument, uriPrefix);
  clearCacheMapByUriPrefix(completionStreamingInFlight, uriPrefix);
  clearCacheMapByUriPrefix(completionPagingState, uriPrefix);
  clearCacheMapByUriPrefix(completionTop1StableByKey, uriPrefix);
  clearCacheMapByUriPrefix(completionRequestStartedAt, uriPrefix);
  clearCacheMapByUriPrefix(completionRequestRefreshed, uriPrefix);
  clearCacheMapByUriPrefix(completionTraceActiveByKey, uriPrefix);
  clearCacheMapByUriPrefix(inlineCompletionCache, inlinePrefix);
  clearCacheMapByUriPrefix(inlineSuggestionCycleByKey, inlinePrefix);
  inlineNextEditBoostByDoc.delete(document.uri.toString());
  clearSetByUriPrefix(completionFirstUsableMarked, uriPrefix);
}

function resetSuggestionRuntimeState(): void {
  completionStreamingCachePrefix.clear();
  completionStreamingCacheContext.clear();
  completionStreamingCacheDocument.clear();
  completionStreamingInFlight.clear();
  completionPagingState.clear();
  completionLoadNextPageKey = undefined;
  completionLoadNextPageIssuedAt = 0;
  completionTop1StableByKey.clear();
  inlineCompletionCache.clear();
  inlineSuggestionCycleByKey.clear();
  inlineNextEditBoostByDoc.clear();
  completionStickyAcceptedByDoc.clear();
  completionLspNegativeCache.clear();
  completionRequestStartedAt.clear();
  completionRequestRefreshed.clear();
  completionFirstUsableMarked.clear();
  completionLatencyWindowMs.length = 0;
  completionStreamingFirstPaintMs.length = 0;
  completionStreamingEnrichMs.length = 0;
  completionFirstUsableMs.length = 0;
  completionColdStartMs.length = 0;
  completionStableListMs.length = 0;
  completionNoRefreshStrictMs.length = 0;
  completionShadowAgreement.length = 0;
  completionShadowDrift.length = 0;
  completionStreamingRefreshCount = 0;
  completionStreamingTimeoutCount = 0;
  completionStreamingTotalCount = 0;
  completionStreamingManualRefreshCount = 0;
  completionStreamingRicherRefreshCount = 0;
  completionStreamingIncompleteRefreshCount = 0;
  completionSuggestionShownCount = 0;
  completionSuggestionAcceptedCount = 0;
  completionSuggestionCanceledCount = 0;
  completionSuggestionPendingAccepted = false;
  completionPendingRejectedItems = [];
  completionShownBySource.clear();
  completionAcceptedBySource.clear();
  completionCacheRequestCount = 0;
  completionCacheHitCount = 0;
  completionPrefetchRequestCount = 0;
  completionPrefetchHitCount = 0;
  completionColdStartCount = 0;
  completionCacheEvictionCount = 0;
  completionCachePressureEvents = 0;
  lastCachePressureLogAt = 0;
  completionFallbackTimeoutCount = 0;
  completionFallbackNegativeCacheCount = 0;
  completionFallbackOfflineCount = 0;
  completionFallbackCancelCount = 0;
  completionTraceActiveByKey.clear();
  completionTraceHistory.length = 0;
  refreshSuggestionProfilerPanel();
}

function flattenDocSymbols(
  symbols: readonly vscode.DocumentSymbol[],
  out: string[],
  parent = "",
): void {
  for (const s of symbols) {
    const seg = `${parent}/${s.kind}:${s.name}:${s.range.start.line}:${s.range.end.line}`;
    out.push(seg);
    if (s.children.length > 0) {
      flattenDocSymbols(s.children, out, `${parent}/${s.name}`);
    }
  }
}

async function computeAstFingerprint(document: vscode.TextDocument): Promise<string | undefined> {
  try {
    const raw = await vscode.commands.executeCommand<(vscode.DocumentSymbol | vscode.SymbolInformation)[] | undefined>(
      "vscode.executeDocumentSymbolProvider",
      document.uri,
    );
    if (!raw || raw.length === 0) return undefined;
    const entries: string[] = [];
    if (raw[0] instanceof vscode.DocumentSymbol) {
      flattenDocSymbols(raw as vscode.DocumentSymbol[], entries, "");
    } else {
      for (const s of raw as vscode.SymbolInformation[]) {
        entries.push(`${s.containerName ?? ""}/${s.kind}:${s.name}:${s.location.range.start.line}:${s.location.range.end.line}`);
      }
    }
    if (entries.length === 0) return undefined;
    entries.sort((a, b) => a.localeCompare(b));
    return `${document.languageId}|${entries.slice(0, 1200).join("|")}`;
  } catch {
    return undefined;
  }
}

function scheduleAstCacheInvalidation(document: vscode.TextDocument, invalidateOnChange: boolean): void {
  const key = document.uri.toString();
  const prev = completionAstRefreshTimers.get(key);
  if (prev) clearTimeout(prev);
  const timer = setTimeout(() => {
    void (async () => {
      const fp = await computeAstFingerprint(document);
      if (!fp) return;
      const old = completionAstFingerprintByDoc.get(key);
      if (invalidateOnChange && old && old !== fp) {
        invalidateCompletionCachesForDocument(document);
      }
      completionAstFingerprintByDoc.set(key, fp);
    })();
    completionAstRefreshTimers.delete(key);
  }, invalidateOnChange ? 220 : 60);
  completionAstRefreshTimers.set(key, timer);
}

function scheduleCompletionIdlePrefetch(editor: vscode.TextEditor | undefined): void {
  if (!editor) return;
  const cfg = vscode.workspace.getConfiguration("vitte");
  const enabled = cfg.get<boolean>("suggestions.idlePrefetch", true);
  if (!enabled) return;
  const radius = Math.max(0, Math.min(6, cfg.get<number>("suggestions.prefetchLineRadius", 1)));
  const directional = cfg.get<boolean>("suggestions.prefetchDirectional", true);
  if (completionIdlePrefetchTimer) clearTimeout(completionIdlePrefetchTimer);
  completionIdlePrefetchTimer = setTimeout(() => {
    void (async () => {
      if (completionPrefetchInFlight) return;
      completionPrefetchInFlight = true;
      try {
        const doc = editor.document;
        const base = editor.selection.active;
        const positions: vscode.Position[] = [];
        const direction = cfg.get<"forward" | "backward">("suggestions.prefetchDirection", "forward");
        const deltas = directional
          ? (direction === "forward"
            ? [0, 1, 2, -1, 3, -2]
            : [0, -1, -2, 1, -3, 2])
          : [];
        if (directional) {
          for (const d of deltas) {
            if (Math.abs(d) > radius) continue;
            const line = base.line + d;
            if (line < 0 || line >= doc.lineCount) continue;
            const lineLen = doc.lineAt(line).text.length;
            const character = Math.min(base.character, lineLen);
            positions.push(new vscode.Position(line, character));
          }
        } else {
          for (let delta = -radius; delta <= radius; delta += 1) {
            const line = base.line + delta;
            if (line < 0 || line >= doc.lineCount) continue;
            const lineLen = doc.lineAt(line).text.length;
            const character = Math.min(base.character, lineLen);
            positions.push(new vscode.Position(line, character));
          }
        }
        completionPrefetchRequestCount += positions.length;
        for (const pos of positions) {
          completionPrefetchExpectedKey = getCompletionRequestKey(doc, pos);
          await vscode.commands.executeCommand<vscode.CompletionList | vscode.CompletionItem[]>(
            "vscode.executeCompletionItemProvider",
            doc.uri,
            pos,
          );
        }
      } catch {
        // Best-effort prefetch.
      } finally {
        completionPrefetchExpectedKey = undefined;
        completionPrefetchInFlight = false;
      }
    })();
  }, 180);
}

function seedDedupKey(item: vscode.CompletionItem): string {
  const label = (typeof item.label === "string" ? item.label : item.label.label).trim().toLowerCase();
  const kind = String(item.kind ?? 0);
  const insert = typeof item.insertText === "string"
    ? item.insertText
    : item.insertText instanceof vscode.SnippetString
      ? item.insertText.value
      : "";
  return `${label}|${kind}|${insert.replace(/\s+/g, " ").trim().toLowerCase()}`;
}

function pushWindowSample(target: number[], value: number, max = 256): void {
  target.push(value);
  if (target.length > max) target.shift();
}

function pushSuggestionTraceHistory(entry: SuggestionTraceEntry, max = 240): void {
  completionTraceHistory.push(entry);
  if (completionTraceHistory.length > max) completionTraceHistory.shift();
}

function getSuggestionTraceHistory(limit = 80): SuggestionTraceEntry[] {
  return completionTraceHistory.slice(Math.max(0, completionTraceHistory.length - limit));
}

function escapeHtml(input: string): string {
  return input
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function renderSuggestionProfilerHtml(): string {
  const rows = getSuggestionTraceHistory(80).reverse();
  const localStats = localInlineEngine?.getStats();
  const aiStats = aiInlinePipeline?.getTelemetry();
  const body = rows.length === 0
    ? `<tr><td colspan="11">No suggestion request captured yet.</td></tr>`
    : rows.map((r) => {
      const ts = new Date(r.startedAt).toLocaleTimeString();
      const p = r.firstPaintMs !== undefined ? `${r.firstPaintMs}ms` : "—";
      const e = r.enrichMs !== undefined ? `${r.enrichMs}ms` : "—";
      const u = r.firstUsableMs !== undefined ? `${r.firstUsableMs}ms` : "—";
      const s = r.stableMs !== undefined ? `${r.stableMs}ms` : "—";
      const strict = r.noRefreshStrict ? "yes" : "no";
      return `<tr>
        <td>${escapeHtml(ts)}</td>
        <td>${escapeHtml(r.languageId)}</td>
        <td title="${escapeHtml(r.uriShort)}">${escapeHtml(r.uriShort)}</td>
        <td>${escapeHtml(p)}</td>
        <td>${escapeHtml(e)}</td>
        <td>${escapeHtml(u)}</td>
        <td>${escapeHtml(s)}</td>
        <td>${escapeHtml(r.refreshCause)}</td>
        <td>${escapeHtml(r.fallbackCause)}</td>
        <td>${r.shownCount}</td>
        <td title="${escapeHtml(r.sourceMix ?? "")}">${escapeHtml(r.sourceMix ?? "—")}</td>
        <td>${escapeHtml(strict)}</td>
      </tr>`;
    }).join("");
  const localStatsHtml = !localStats
    ? `<div class="muted">Local inline engine stats: unavailable.</div>`
    : `<div class="cards">
        <div class="card"><div class="k">Local Requests</div><div class="v">${localStats.requests}</div></div>
        <div class="card"><div class="k">Cache Hit Rate</div><div class="v">${(localStats.cacheHitRate * 100).toFixed(1)}%</div></div>
        <div class="card"><div class="k">Local P50</div><div class="v">${localStats.latencyP50Ms}ms</div></div>
        <div class="card"><div class="k">Local P95</div><div class="v">${localStats.latencyP95Ms}ms</div></div>
        <div class="card"><div class="k">Indexed Files</div><div class="v">${localStats.indexedFiles}</div></div>
        <div class="card"><div class="k">Learned Contexts</div><div class="v">${localStats.acceptanceContexts}</div></div>
        <div class="card"><div class="k">Learned Entries</div><div class="v">${localStats.acceptanceEntries}</div></div>
        <div class="card"><div class="k">Hot Cache Entries</div><div class="v">${localStats.hotCacheEntries}</div></div>
      </div>`;
  const aiStatsHtml = !aiStats
    ? `<div class="muted">AI pipeline stats: unavailable.</div>`
    : `<div class="cards">
        <div class="card"><div class="k">AI Requests</div><div class="v">${aiStats.requests}</div></div>
        <div class="card"><div class="k">Backend Req</div><div class="v">${aiStats.backendRequests}</div></div>
        <div class="card"><div class="k">Backend Fail</div><div class="v">${aiStats.backendFailures}</div></div>
        <div class="card"><div class="k">Timeouts</div><div class="v">${aiStats.backendTimeouts}</div></div>
        <div class="card"><div class="k">Fallback Local</div><div class="v">${aiStats.fallbackLocalCount}</div></div>
        <div class="card"><div class="k">AI Avg/P95</div><div class="v">${aiStats.avgLatencyMs}/${aiStats.p95LatencyMs}ms</div></div>
        <div class="card"><div class="k">CPU Avg/P95</div><div class="v">${aiStats.avgCpuMicros}/${aiStats.p95CpuMicros}us</div></div>
        <div class="card"><div class="k">MemΔ Avg/P95</div><div class="v">${aiStats.avgMemDeltaKb}/${aiStats.p95MemDeltaKb}KB</div></div>
        <div class="card"><div class="k">RAG Chunks</div><div class="v">${aiStats.chunks}</div></div>
      </div>`;
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body { font-family: var(--vscode-font-family); font-size: 12px; margin: 12px; color: var(--vscode-foreground); background: var(--vscode-editor-background); }
    h2 { margin: 0 0 10px 0; font-size: 14px; }
    .muted { opacity: .8; margin-bottom: 10px; }
    .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 8px; margin: 10px 0 12px 0; }
    .card { border: 1px solid var(--vscode-panel-border); border-radius: 6px; padding: 6px 8px; background: var(--vscode-editorWidget-background); }
    .k { opacity: .8; font-size: 11px; }
    .v { font-size: 14px; font-weight: 600; margin-top: 3px; }
    table { border-collapse: collapse; width: 100%; table-layout: fixed; }
    th, td { border: 1px solid var(--vscode-panel-border); padding: 4px 6px; text-align: left; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    th { position: sticky; top: 0; background: var(--vscode-sideBar-background); z-index: 1; }
  </style>
</head>
<body>
  <h2>Vitte Suggestion Profiler</h2>
  <div class="muted">Timeline per request: first paint, enrich, first usable, stability, refresh cause, fallback cause.</div>
  ${localStatsHtml}
  ${aiStatsHtml}
  <table>
    <thead>
      <tr>
        <th>Time</th><th>Lang</th><th>File</th><th>First paint</th><th>Enrich</th><th>First usable</th><th>Stable</th><th>Refresh</th><th>Fallback</th><th>Shown</th><th>Sources</th><th>No-refresh</th>
      </tr>
    </thead>
    <tbody>${body}</tbody>
  </table>
</body>
</html>`;
}

function refreshSuggestionProfilerPanel(): void {
  if (!suggestionProfilerPanel) return;
  suggestionProfilerPanel.webview.html = renderSuggestionProfilerHtml();
}

function openSuggestionProfilerPanel(context: vscode.ExtensionContext): void {
  if (suggestionProfilerPanel) {
    suggestionProfilerPanel.reveal(vscode.ViewColumn.Beside);
    refreshSuggestionProfilerPanel();
    return;
  }
  suggestionProfilerPanel = vscode.window.createWebviewPanel(
    "vitteSuggestionProfiler",
    "Vitte Suggestion Profiler",
    vscode.ViewColumn.Beside,
    { enableFindWidget: true },
  );
  refreshSuggestionProfilerPanel();
  suggestionProfilerRenderTimer = setInterval(() => {
    refreshSuggestionProfilerPanel();
  }, 1200);
  suggestionProfilerPanel.onDidDispose(() => {
    suggestionProfilerPanel = undefined;
    if (suggestionProfilerRenderTimer) {
      clearInterval(suggestionProfilerRenderTimer);
      suggestionProfilerRenderTimer = undefined;
    }
  }, undefined, context.subscriptions);
}

function getAdaptiveRefreshDebounceMs(documentUri: string, minMs: number, maxMs: number): number {
  const sample = typingSpeedByDoc.get(documentUri);
  if (!sample) return minMs;
  // Faster typing (small interval) => larger debounce.
  const interval = Math.max(20, Math.min(1200, sample.intervalEwmaMs));
  const normalized = 1 - Math.min(1, Math.max(0, (interval - 60) / 540));
  return Math.round(minMs + (maxMs - minMs) * normalized);
}

function listAgreementAndDrift(
  primary: vscode.CompletionItem[],
  shadow: vscode.CompletionItem[],
  topK = 10,
): { agreement: number; drift: number } {
  const a = primary.slice(0, topK).map((it) => normalizeCompletionLabel(it));
  const b = shadow.slice(0, topK).map((it) => normalizeCompletionLabel(it));
  if (a.length === 0 || b.length === 0) return { agreement: 0, drift: 0 };
  const bIdx = new Map<string, number>();
  for (let i = 0; i < b.length; i += 1) bIdx.set(b[i]!, i);
  let common = 0;
  let driftSum = 0;
  for (let i = 0; i < a.length; i += 1) {
    const j = bIdx.get(a[i]!);
    if (j === undefined) continue;
    common += 1;
    driftSum += Math.abs(i - j);
  }
  return {
    agreement: common / Math.max(1, Math.min(a.length, b.length)),
    drift: common > 0 ? driftSum / common : topK,
  };
}

function buildLoadMoreCompletionItem(): vscode.CompletionItem {
  const item = new vscode.CompletionItem("Load next suggestions page…", vscode.CompletionItemKind.Reference);
  item.detail = "[source:ui] pagination";
  item.sortText = "zzzzzz_load_more";
  item.filterText = "load next suggestions page";
  item.insertText = "";
  item.command = {
    command: "vitte.suggestions.loadNextPage",
    title: "Load next suggestions page",
  };
  item.documentation = new vscode.MarkdownString("Load more completion results for the current prefix.");
  return item;
}

function updateTop1StableInlineCandidate(
  requestKey: string,
  items: vscode.CompletionItem[],
): void {
  const real = items.find((it) => extractItemSource(it) !== "ui");
  if (!real) {
    completionTop1StableByKey.delete(requestKey);
    return;
  }
  const text = extractCompletionInsertText(real).trim();
  if (!text || text.includes("\n") || text.includes("\t$")) {
    completionTop1StableByKey.delete(requestKey);
    return;
  }
  const prev = completionTop1StableByKey.get(requestKey);
  const now = Date.now();
  if (prev?.text !== text || (now - (prev?.lastAt ?? 0)) > 5000) {
    completionTop1StableByKey.set(requestKey, { text, count: 1, lastAt: now });
    return;
  }
  completionTop1StableByKey.set(requestKey, { text, count: Math.min(12, prev.count + 1), lastAt: now });
}

function completeCompletionRequestMetrics(key: string, endedAt: number, expectedStartedAt?: number): void {
  const started = completionRequestStartedAt.get(key);
  if (expectedStartedAt !== undefined && started !== expectedStartedAt) return;
  const trace = completionTraceActiveByKey.get(key);
  if (expectedStartedAt !== undefined && trace?.startedAt !== expectedStartedAt) return;
  if (started !== undefined) {
    const refreshed = completionRequestRefreshed.get(key) ?? false;
    if (!refreshed) {
      pushWindowSample(completionNoRefreshStrictMs, endedAt - started);
    }
    if (trace) {
      trace.noRefreshStrict = !refreshed;
      trace.stableMs ??= Math.max(0, endedAt - trace.startedAt);
      pushSuggestionTraceHistory({ ...trace });
      refreshSuggestionProfilerPanel();
    }
  }
  completionRequestStartedAt.delete(key);
  completionRequestRefreshed.delete(key);
  completionFirstUsableMarked.delete(key);
  completionTraceActiveByKey.delete(key);
}

function percentileLocal(values: number[], q: number): number {
  if (values.length === 0) return 0;
  const xs = [...values].sort((a, b) => a - b);
  const i = Math.max(0, Math.min(xs.length - 1, Math.round((xs.length - 1) * q)));
  return xs[i] ?? 0;
}

function inlineHeuristicCandidate(document: vscode.TextDocument, position: vscode.Position): string | undefined {
  const line = document.lineAt(position.line).text;
  const left = line.slice(0, position.character).trim();
  if (/^if\s+[^{]*$/.test(left)) return " {\n\t\n}";
  if (/^for\s+[^{]*$/.test(left)) return " {\n\t\n}";
  if (/^while\s+[^{]*$/.test(left)) return " {\n\t\n}";
  if (/^match\s+[^{]*$/.test(left)) return " {\n\t_ => \n}";
  if (/^(proc|fn)\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s*$/.test(left)) return " {\n\t\n}";
  if (/^test\s+\"[^\"]*\"\s*$/.test(left)) return " {\n\t\n}";
  return undefined;
}

function inlineNextEditCandidate(document: vscode.TextDocument, position: vscode.Position): string | undefined {
  const current = document.lineAt(position.line).text.slice(0, position.character).trim();
  if (/^(proc|fn)\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s*$/.test(current)) return " {\n\t\n}";
  if (current.endsWith("{")) return "\n\t\n}";
  if (/;\s*$/.test(current)) {
    for (let i = position.line; i >= Math.max(0, position.line - 20); i -= 1) {
      const line = document.lineAt(i).text.trim();
      const m = /\blet\s+([A-Za-z_][A-Za-z0-9_]*)\b/.exec(line);
      if (m?.[1]) return `\nreturn ${m[1]};`;
    }
    return "\n";
  }
  return undefined;
}

function readLocalInlineEngineOptions(): LocalInlineEngineOptions {
  const cfg = vscode.workspace.getConfiguration("vitte");
  return {
    enabled: cfg.get<boolean>("suggestions.localEngine.enabled", true),
    maxFiles: Math.max(50, Math.min(50000, cfg.get<number>("suggestions.localEngine.maxFiles", 6000))),
    maxFileSizeKB: Math.max(16, Math.min(8192, cfg.get<number>("suggestions.localEngine.maxFileSizeKB", 256))),
    maxSuggestions: Math.max(1, Math.min(24, cfg.get<number>("suggestions.localEngine.inlineTopK", 8))),
    reindexDebounceMs: Math.max(25, Math.min(3000, cfg.get<number>("suggestions.localEngine.reindexDebounceMs", 120))),
    persistIndex: cfg.get<boolean>("suggestions.localEngine.persistIndex", true),
    expectedValueHints: cfg.get<boolean>("suggestions.localEngine.expectedValueHints", true),
    errorRecoveryHints: cfg.get<boolean>("suggestions.localEngine.errorRecoveryHints", true),
    multilineEnabled: cfg.get<boolean>("suggestions.localEngine.multilineEnabled", true),
    beamWidth: Math.max(2, Math.min(16, cfg.get<number>("suggestions.localEngine.beamWidth", 6))),
    maxGeneratedTokens: Math.max(4, Math.min(64, cfg.get<number>("suggestions.localEngine.maxGeneratedTokens", 24))),
    apiChainHints: cfg.get<boolean>("suggestions.localEngine.apiChainHints", true),
    adaptiveLearning: cfg.get<boolean>("suggestions.localEngine.adaptiveLearning", true),
    contextClassifierEnabled: cfg.get<boolean>("suggestions.localEngine.contextClassifierEnabled", true),
    fastCacheEnabled: cfg.get<boolean>("suggestions.localEngine.fastCacheEnabled", true),
    fastCacheSize: Math.max(32, Math.min(4096, cfg.get<number>("suggestions.localEngine.fastCacheSize", 512))),
    fastCacheTtlMs: Math.max(20, Math.min(5000, cfg.get<number>("suggestions.localEngine.fastCacheTtlMs", 400))),
  };
}

function readAiInlinePipelineOptions(): AiInlinePipelineOptions {
  const cfg = vscode.workspace.getConfiguration("vitte");
  return {
    enabled: cfg.get<boolean>("suggestions.aiPipeline.enabled", false),
    backendEnabled: cfg.get<boolean>("suggestions.aiPipeline.backendEnabled", false),
    backendUrl: cfg.get<string>("suggestions.aiPipeline.backendUrl", ""),
    backendTimeoutMs: Math.max(100, Math.min(10000, cfg.get<number>("suggestions.aiPipeline.backendTimeoutMs", 800))),
    ragTopK: Math.max(1, Math.min(24, cfg.get<number>("suggestions.aiPipeline.ragTopK", 6))),
    maxCandidates: Math.max(1, Math.min(16, cfg.get<number>("suggestions.aiPipeline.maxCandidates", 6))),
    antiHallucination: cfg.get<boolean>("suggestions.aiPipeline.antiHallucination", true),
    allowUnknownSymbols: cfg.get<boolean>("suggestions.aiPipeline.allowUnknownSymbols", false),
    styleMode: cfg.get<"project" | "user">("suggestions.aiPipeline.styleMode", "project"),
    userMode: cfg.get<"aggressive" | "balanced" | "conservative">("suggestions.aiPipeline.userMode", "balanced"),
    granularity: cfg.get<"classic_only" | "inline_only" | "hybrid">("suggestions.granularity", "hybrid"),
    privacyStrict: cfg.get<boolean>("suggestions.aiPipeline.privacyStrict", true),
    backendAllowlist: cfg.get<string[]>("suggestions.aiPipeline.backendAllowlist", []),
    redactSecrets: cfg.get<boolean>("suggestions.aiPipeline.redactSecrets", true),
    trustedWorkspaceOnly: cfg.get<boolean>("suggestions.aiPipeline.trustedWorkspaceOnly", true),
    cloudOptIn: cfg.get<boolean>("suggestions.aiPipeline.cloudOptIn", false),
    localOnly: cfg.get<boolean>("suggestions.aiPipeline.localOnly", true),
    dataRetentionDays: Math.max(0, Math.min(3650, cfg.get<number>("suggestions.aiPipeline.dataRetentionDays", 0))),
    allowExternalTraining: cfg.get<boolean>("suggestions.aiPipeline.allowExternalTraining", false),
    useWorkerIndexing: cfg.get<boolean>("suggestions.aiPipeline.useWorkerIndexing", true),
    promptCacheEnabled: cfg.get<boolean>("suggestions.aiPipeline.promptCacheEnabled", true),
    promptCacheTtlMs: Math.max(20, Math.min(5000, cfg.get<number>("suggestions.aiPipeline.promptCacheTtlMs", 400))),
    promptCacheSize: Math.max(16, Math.min(4096, cfg.get<number>("suggestions.aiPipeline.promptCacheSize", 512))),
  };
}

function updateSuggestionEngineModeSuffix(): void {
  const cfg = vscode.workspace.getConfiguration("vitte");
  const inline = cfg.get<boolean>("suggestions.ghostTextTop1Stable", true);
  const ai = cfg.get<boolean>("suggestions.aiPipeline.enabled", false);
  const backend = cfg.get<boolean>("suggestions.aiPipeline.backendEnabled", false);
  const localOnly = cfg.get<boolean>("suggestions.aiPipeline.localOnly", true);
  const cloudOptIn = cfg.get<boolean>("suggestions.aiPipeline.cloudOptIn", false);
  if (!inline) suggestionEngineModeSuffix = "inline-off";
  else if (localOnly) suggestionEngineModeSuffix = "local-only";
  else if (ai && backend && !cloudOptIn) suggestionEngineModeSuffix = "cloud-locked";
  else if (ai && backend) suggestionEngineModeSuffix = "cloud";
  else if (ai) suggestionEngineModeSuffix = "hybrid-local";
  else suggestionEngineModeSuffix = "local";
  applyStatusBar();
}

async function ensureCloudOptInIfNeeded(): Promise<void> {
  const cfg = vscode.workspace.getConfiguration("vitte");
  const enabled = cfg.get<boolean>("suggestions.aiPipeline.enabled", false);
  const backend = cfg.get<boolean>("suggestions.aiPipeline.backendEnabled", false);
  const localOnly = cfg.get<boolean>("suggestions.aiPipeline.localOnly", true);
  const optIn = cfg.get<boolean>("suggestions.aiPipeline.cloudOptIn", false);
  if (!enabled || !backend || localOnly || optIn) return;
  const choice = await vscode.window.showWarningMessage(
    "Vitte AI cloud backend is configured but disabled until explicit opt-in.",
    "Enable Cloud",
    "Keep Local-Only",
  );
  if (choice === "Enable Cloud") {
    await cfg.update("suggestions.aiPipeline.cloudOptIn", true, vscode.ConfigurationTarget.Workspace);
    await cfg.update("suggestions.aiPipeline.localOnly", false, vscode.ConfigurationTarget.Workspace);
    updateSuggestionEngineModeSuffix();
  }
}

function scheduleLocalInlineEngineUpdate(document: vscode.TextDocument): void {
  if (!localInlineEngine) return;
  if (!isVitteDocument(document)) return;
  const key = document.uri.toString();
  const prev = localInlineReindexTimers.get(key);
  if (prev) clearTimeout(prev);
  const waitMs = Math.max(25, Math.min(3000, readLocalInlineEngineOptions().reindexDebounceMs));
  const timer = setTimeout(() => {
    localInlineEngine?.upsertDocument(document);
    void aiInlinePipeline?.upsert(document.uri.toString(), document.getText());
    localInlineReindexTimers.delete(key);
  }, waitMs);
  localInlineReindexTimers.set(key, timer);
}

function dedupeInlineTexts(items: vscode.InlineCompletionItem[]): vscode.InlineCompletionItem[] {
  const seen = new Set<string>();
  const out: vscode.InlineCompletionItem[] = [];
  for (const it of items) {
    const text = typeof it.insertText === "string" ? it.insertText : it.insertText.value;
    if (seen.has(text)) continue;
    seen.add(text);
    out.push(it);
  }
  return out;
}

function rotateInlineItems(items: vscode.InlineCompletionItem[], offset: number): vscode.InlineCompletionItem[] {
  if (items.length <= 1) return items;
  const n = items.length;
  const k = ((offset % n) + n) % n;
  if (k === 0) return items;
  return [...items.slice(k), ...items.slice(0, k)];
}

function getStreamingCompletionStats(): {
  firstPaintP50Ms: number;
  firstPaintP95Ms: number;
  enrichP50Ms: number;
  enrichP95Ms: number;
  refreshCount: number;
  refreshManualCount: number;
  refreshRicherCount: number;
  refreshIncompleteCount: number;
  timeoutCount: number;
  totalCount: number;
  timeoutRate: number;
  ttfsP50Ms: number;
  ttfsP95Ms: number;
  enrichDeltaP50Ms: number;
  enrichDeltaP95Ms: number;
  acceptanceRate: number;
  cancelRate: number;
  acceptanceBySource: string;
  cacheHitRate: number;
  prefetchHitRate: number;
  coldStartCount: number;
  coldStartP50Ms: number;
  coldStartP95Ms: number;
  stableListP50Ms: number;
  stableListP95Ms: number;
  cacheEvictions: number;
  cachePressureEvents: number;
  firstUsableP50Ms: number;
  firstUsableP95Ms: number;
  noRefreshStrictP50Ms: number;
  noRefreshStrictP95Ms: number;
  fallbackTimeoutCount: number;
  fallbackNegativeCacheCount: number;
  fallbackOfflineCount: number;
  fallbackCancelCount: number;
  shadowAgreementP50: number;
  shadowAgreementP95: number;
  shadowDriftP50: number;
  shadowDriftP95: number;
} {
  const total = Math.max(1, completionStreamingTotalCount);
  const totalShown = Math.max(1, completionSuggestionShownCount);
  const cacheReq = Math.max(1, completionCacheRequestCount);
  const prefetchReq = Math.max(1, completionPrefetchRequestCount);
  const acceptanceBySource = ["local", "cache", "lsp", "plugin", "unknown"]
    .map((src) => {
      const shown = completionShownBySource.get(src) ?? 0;
      const accepted = completionAcceptedBySource.get(src) ?? 0;
      const rate = shown > 0 ? (accepted / shown) * 100 : 0;
      return `${src}:${rate.toFixed(1)}%`;
    })
    .join(" | ");
  return {
    firstPaintP50Ms: percentileLocal(completionStreamingFirstPaintMs, 0.5),
    firstPaintP95Ms: percentileLocal(completionStreamingFirstPaintMs, 0.95),
    enrichP50Ms: percentileLocal(completionStreamingEnrichMs, 0.5),
    enrichP95Ms: percentileLocal(completionStreamingEnrichMs, 0.95),
    refreshCount: completionStreamingRefreshCount,
    refreshManualCount: completionStreamingManualRefreshCount,
    refreshRicherCount: completionStreamingRicherRefreshCount,
    refreshIncompleteCount: completionStreamingIncompleteRefreshCount,
    timeoutCount: completionStreamingTimeoutCount,
    totalCount: completionStreamingTotalCount,
    timeoutRate: completionStreamingTimeoutCount / total,
    ttfsP50Ms: percentileLocal(completionStreamingFirstPaintMs, 0.5),
    ttfsP95Ms: percentileLocal(completionStreamingFirstPaintMs, 0.95),
    enrichDeltaP50Ms: percentileLocal(completionStreamingEnrichMs, 0.5),
    enrichDeltaP95Ms: percentileLocal(completionStreamingEnrichMs, 0.95),
    acceptanceRate: completionSuggestionAcceptedCount / totalShown,
    cancelRate: completionSuggestionCanceledCount / totalShown,
    acceptanceBySource,
    cacheHitRate: completionCacheHitCount / cacheReq,
    prefetchHitRate: completionPrefetchHitCount / prefetchReq,
    coldStartCount: completionColdStartCount,
    coldStartP50Ms: percentileLocal(completionColdStartMs, 0.5),
    coldStartP95Ms: percentileLocal(completionColdStartMs, 0.95),
    stableListP50Ms: percentileLocal(completionStableListMs, 0.5),
    stableListP95Ms: percentileLocal(completionStableListMs, 0.95),
    cacheEvictions: completionCacheEvictionCount,
    cachePressureEvents: completionCachePressureEvents,
    firstUsableP50Ms: percentileLocal(completionFirstUsableMs, 0.5),
    firstUsableP95Ms: percentileLocal(completionFirstUsableMs, 0.95),
    noRefreshStrictP50Ms: percentileLocal(completionNoRefreshStrictMs, 0.5),
    noRefreshStrictP95Ms: percentileLocal(completionNoRefreshStrictMs, 0.95),
    fallbackTimeoutCount: completionFallbackTimeoutCount,
    fallbackNegativeCacheCount: completionFallbackNegativeCacheCount,
    fallbackOfflineCount: completionFallbackOfflineCount,
    fallbackCancelCount: completionFallbackCancelCount,
    shadowAgreementP50: percentileLocal(completionShadowAgreement, 0.5),
    shadowAgreementP95: percentileLocal(completionShadowAgreement, 0.95),
    shadowDriftP50: percentileLocal(completionShadowDrift, 0.5),
    shadowDriftP95: percentileLocal(completionShadowDrift, 0.95),
  };
}

interface ExtensionManifest {
  version?: string;
}

function getExtensionVersion(ext: vscode.Extension<unknown> | undefined): string {
  const manifest: unknown = ext?.packageJSON;
  if (!manifest || typeof manifest !== "object") return "unknown";
  const version = (manifest as ExtensionManifest).version;
  return typeof version === "string" && version.length > 0 ? version : "unknown";
}

export interface ExtensionApi {
  apiVersion: "1.0.0";
  extensionVersion: string;
  capabilities: {
    status: true;
    restart: true;
    runAction: true;
    serverResolutionTestHook: true;
  };
  getStatusText(): string;
  getStatusTooltip(): string;
  getClientState(): ClientState | undefined;
  runAction(action: string): Promise<void>;
  restart(): Promise<void>;
  resolveServerModuleForTest(ctx: Pick<vscode.ExtensionContext, "asAbsolutePath">): string;
}

const LANGUAGES = ["vitte", "vit"] as const;
const WATCH_PATTERNS = [
  "**/*.{vitte,vit,vitl}",
  "**/vitte.toml",
  "**/.vitte/config",
  "**/.vitteconfig"
] as const;
const LANGUAGE_SET = new Set<string>(LANGUAGES);
const RESTART_RELEVANT_CONFIG_KEYS = [
  "vitte.serverPath",
  "vitte.server.offline",
  "vitte.server.offlinePermanent",
  "vitte.requestTimeoutMs",
  "vitte.requestMaxConcurrent",
  "vitte.requestTimeouts",
  "vitte.trace.server",
  "vitte.indexerMaxRssMB",
  "vitte.indexerCacheEnabled",
  "vitte.channel",
  "vitte.features",
  "vitte.lsp.path",
  "vitte.lang",
  "vitte.policy.path",
  "vitte.policy.json",
] as const;

interface CommandMenuEntry {
  label: string;
  description?: string;
  detail?: string;
  command: string;
}

interface CommandMenuItem extends vscode.QuickPickItem {
  command: string;
}

type ObsLevel = "info" | "warn" | "error";
interface ObsEntry {
  ts: string;
  event: string;
  lvl: ObsLevel;
  requestId?: string;
  data?: Record<string, unknown>;
}

const OBS_HISTORY_MAX = 400;
const obsHistory: ObsEntry[] = [];
let obsSeq = 0;

function nextRequestId(prefix = "req"): string {
  obsSeq += 1;
  return `${prefix}-${Date.now().toString(36)}-${obsSeq.toString(36)}`;
}

function obsLog(event: string, lvl: ObsLevel = "info", data?: Record<string, unknown>, requestId?: string): void {
  const entry: ObsEntry = {
    ts: new Date().toISOString(),
    event,
    lvl,
  };
  if (requestId) entry.requestId = requestId;
  if (data && Object.keys(data).length > 0) entry.data = data;
  obsHistory.push(entry);
  if (obsHistory.length > OBS_HISTORY_MAX) {
    obsHistory.splice(0, obsHistory.length - OBS_HISTORY_MAX);
  }
  try {
    output.appendLine(`[obs] ${JSON.stringify(entry)}`);
  } catch {
    // noop: output channel may not be ready during very early activation
  }
}

function readNumberSetting(cfg: vscode.WorkspaceConfiguration, key: string, fallback: number): number {
  const raw = cfg.get<unknown>(key);
  if (typeof raw !== "number" || !Number.isFinite(raw)) return fallback;
  return raw;
}

function validateRuntimeSettings(): string[] {
  const cfg = vscode.workspace.getConfiguration("vitte");
  const issues: string[] = [];

  const topN = readNumberSetting(cfg, "suggestions.topN", 50);
  const minN = readNumberSetting(cfg, "suggestions.minN", 20);
  const maxN = readNumberSetting(cfg, "suggestions.maxN", 80);
  if (minN > topN) issues.push(`vitte.suggestions.minN (${minN}) should be <= vitte.suggestions.topN (${topN}).`);
  if (topN > maxN) issues.push(`vitte.suggestions.topN (${topN}) should be <= vitte.suggestions.maxN (${maxN}).`);

  const pageSize = readNumberSetting(cfg, "suggestions.pageSize", 20);
  if (pageSize < 5 || pageSize > 200) {
    issues.push(`vitte.suggestions.pageSize (${pageSize}) is outside supported range [5..200].`);
  }

  const fnRatio = readNumberSetting(cfg, "suggestions.functionBudgetRatio", 0.45);
  const varRatio = readNumberSetting(cfg, "suggestions.variableBudgetRatio", 0.35);
  const snipRatio = readNumberSetting(cfg, "suggestions.snippetBudgetRatio", 0.15);
  const ratios = [
    ["functionBudgetRatio", fnRatio],
    ["variableBudgetRatio", varRatio],
    ["snippetBudgetRatio", snipRatio],
  ] as const;
  for (const [name, value] of ratios) {
    if (value < 0 || value > 1) {
      issues.push(`vitte.suggestions.${name} (${value}) must be in [0..1].`);
    }
  }
  if ((fnRatio + varRatio + snipRatio) > 1.2) {
    issues.push(`vitte.suggestions.*BudgetRatio sum (${(fnRatio + varRatio + snipRatio).toFixed(2)}) is too high (max 1.20).`);
  }

  const lspHardTimeoutMs = readNumberSetting(cfg, "suggestions.lspHardTimeoutMs", 120);
  if (lspHardTimeoutMs < 50 || lspHardTimeoutMs > 1000) {
    issues.push(`vitte.suggestions.lspHardTimeoutMs (${lspHardTimeoutMs}) is outside supported range [50..1000].`);
  }

  const liveDelayMs = readNumberSetting(cfg, "syntax.liveDiagnosticsDelayMs", 120);
  if (liveDelayMs < 60 || liveDelayMs > 2000) {
    issues.push(`vitte.syntax.liveDiagnosticsDelayMs (${liveDelayMs}) is outside supported range [60..2000].`);
  }

  const serverPath = cfg.get<string>("serverPath", "").trim();
  if (serverPath && !fs.existsSync(serverPath)) {
    issues.push(`vitte.serverPath points to a missing file: ${serverPath}`);
  }
  const toolchainRoot = cfg.get<string>("toolchain.root", "").trim();
  if (toolchainRoot && !fs.existsSync(toolchainRoot)) {
    issues.push(`vitte.toolchain.root points to a missing directory: ${toolchainRoot}`);
  }

  return issues;
}

function reportSettingsIssues(outputChannel: vscode.OutputChannel, phase: "activate" | "config"): void {
  const issues = validateRuntimeSettings();
  const digest = issues.join(" | ");
  if (digest === lastSettingsIssueDigest) return;
  lastSettingsIssueDigest = digest;
  if (issues.length === 0) return;
  for (const issue of issues) {
    outputChannel.appendLine(`[settings:${phase}] ${issue}`);
  }
  const suffix = issues.length > 1 ? ` (+${issues.length - 1} more)` : "";
  void vscode.window.showWarningMessage(`Vitte settings validation: ${issues[0]}${suffix}`);
}

interface CommandShortcutConfig {
  label: string;
  command: string;
  icon?: string;
  tooltip?: string;
  statusBar?: boolean;
  startup?: boolean;
}

interface CommandMessageItem extends vscode.MessageItem {
  command?: string;
}

const DEFAULT_COMMAND_SHORTCUTS: readonly CommandShortcutConfig[] = [
  { label: "Build", command: "vitte.build", icon: "$(tools)", tooltip: "Build the workspace (vitte.build)", statusBar: true, startup: true },
  { label: "Run", command: "vitte.run", icon: "$(debug-start)", tooltip: "Build and run the entry point (vitte.run)", statusBar: true, startup: true },
  { label: "Test", command: "vitte.test", icon: "$(beaker)", tooltip: "Execute the test suite (vitte.test)", statusBar: true, startup: true },
] as const;

const COMMAND_MENU_ENTRIES: readonly CommandMenuEntry[] = [
  { label: "Project Assistant", description: "Create project/module/package", command: "vitte.projectAssistant" },
  { label: "New Project", description: "Scaffold a new Vitte project", command: "vitte.newProject" },
  { label: "New Module", description: "Scaffold a new module file", command: "vitte.newModule" },
  { label: "New Package", description: "Scaffold a new package", command: "vitte.newPackage" },
  { label: "Generate VS Code Config", description: "Create .vscode/tasks.json + launch.json", command: "vitte.generateWorkspaceConfig" },
  { label: "Doctor", description: "check + lint + fmt + test + perf", command: "vitte.doctor" },
  { label: "Clean workspace", description: "Remove build outputs", command: "vitte.clean" },
  { label: "Bench workspace", description: "Run vitte.bench", command: "vitte.bench" },
  { label: "Bench extension CI", description: "Activation/memory/completion latency snapshot", command: "vitte.benchExtensionCi" },
  { label: "Export perf session", description: "Export activation/p95/memory session JSON", command: "vitte.exportPerfSession" },
  { label: "Export observability session", description: "Export structured logs + runtime metrics snapshot", command: "vitte.observability.export" },
  { label: "Open bench report", description: "Latest bench report", command: "vitte.benchReport" },
  { label: "Diagnostics ▸ Refresh", description: "Re-scan diagnostics", command: "vitte.diagnostics.refresh" },
  { label: "Diagnostics ▸ Refresh help cache", description: "Clear and reload diagnostics explain cache", command: "vitte.diagnostics.refreshHelpCache" },
  { label: "Diagnostics ▸ Export snapshot", description: "Export workspace diagnostics JSON snapshot", command: "vitte.diagnostics.exportSnapshot" },
  { label: "Diagnostics ▸ Export explain bundle", description: "Export diagnostics + explain bundle for bug report", command: "vitte.diagnostics.exportExplainBundle" },
  { label: "Diagnostics ▸ First error in file", description: "Jump to first error in active file", command: "vitte.diagnostics.goToFirstErrorInFile" },
  { label: "Diagnostics ▸ Next issue", description: "Jump to next diagnostic", command: "editor.action.marker.next" },
  { label: "Quick Actions", description: "Interactive menu", command: "vitte.quickActions" },
  { label: "Server log", description: "Open log output", command: "vitte.showServerLog" },
  { label: "Server metrics", description: "Show performance snapshot", command: "vitte.showServerMetrics" },
  { label: "Suggestions ▸ Load next page", description: "Increment completion page and refresh suggest", command: "vitte.suggestions.loadNextPage" },
  { label: "Inline ▸ Next suggestion", description: "Cycle inline ghost suggestion candidates", command: "vitte.inline.nextSuggestion" },
  { label: "Inline ▸ Next edit", description: "Prioritize next-edit inline ghost suggestion", command: "vitte.inline.nextEdit" },
  { label: "Suggestions ▸ Reset learning", description: "Clear local suggestion learning state", command: "vitte.suggestions.resetLearning" },
  { label: "Suggestions ▸ Export diagnostics", description: "Export suggestion diagnostics JSON snapshot", command: "vitte.suggestions.exportDiagnostics" },
  { label: "Suggestions ▸ Open profiler", description: "Open request timeline for suggestions", command: "vitte.suggestions.openProfiler" },
  { label: "Detect toolchain", description: "Scan for Vitte runtimes", command: "vitte.detectToolchain" },
];

let fileWatchers: vscode.FileSystemWatcher[] = [];
let commandButtonItems: vscode.StatusBarItem[] = [];
let commandMenuButton: vscode.StatusBarItem | undefined;
const DIAGNOSTICS_REFRESH_KEY = "vitte.quickActions.diagRefresh";

function logServerResolution(message: string): void {
  const text = `[vitte] ${message}`;
  output?.appendLine(text);
}

function readCommandShortcuts(): CommandShortcutConfig[] {
  const cfg = vscode.workspace.getConfiguration("vitte");
  const raw = cfg.get<unknown>("commandShortcuts");
  if (!Array.isArray(raw)) {
    return DEFAULT_COMMAND_SHORTCUTS.map((item) => ({ ...item }));
  }
  const sanitized: CommandShortcutConfig[] = [];
  for (const entry of raw) {
    if (!entry || typeof entry !== "object") continue;
    const data = entry as Partial<CommandShortcutConfig>;
    const label = typeof data.label === "string" && data.label.trim().length > 0 ? data.label.trim() : undefined;
    const command = typeof data.command === "string" && data.command.trim().length > 0 ? data.command.trim() : undefined;
    if (!label || !command) continue;
    const shortcut: CommandShortcutConfig = { label, command };
    const icon = typeof data.icon === "string" && data.icon.trim().length > 0 ? data.icon.trim() : undefined;
    if (icon) shortcut.icon = icon;
    const tooltip = typeof data.tooltip === "string" && data.tooltip.trim().length > 0 ? data.tooltip : undefined;
    if (tooltip) shortcut.tooltip = tooltip;
    if (typeof data.statusBar === "boolean") shortcut.statusBar = data.statusBar;
    if (typeof data.startup === "boolean") shortcut.startup = data.startup;
    sanitized.push(shortcut);
  }
  return sanitized.length > 0 ? sanitized : DEFAULT_COMMAND_SHORTCUTS.map((item) => ({ ...item }));
}

function getWorkspaceId(): string | undefined {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function readDiagnosticsRefreshState(context: vscode.ExtensionContext): { lastRefresh?: number; stale: boolean } {
  const workspaceId = getWorkspaceId();
  if (!workspaceId) return { stale: true };
  const map = context.globalState.get<Record<string, number>>(DIAGNOSTICS_REFRESH_KEY, {});
  const ts = map[workspaceId];
  if (!ts) return { stale: true };
  const stale = Date.now() - ts > 10 * 60 * 1000;
  return { lastRefresh: ts, stale };
}

function labelForCommand(command: string): string {
  switch (command) {
    case "vitte.build": return "Build";
    case "vitte.run": return "Run";
    case "vitte.test": return "Run Tests";
    case "vitte.diagnostics.refresh": return "Refresh Diagnostics";
    case "vitte.diagnostics.refreshHelpCache": return "Refresh Diagnostics Help Cache";
    case "vitte.diagnostics.exportSnapshot": return "Export Diagnostics Snapshot";
    case "vitte.diagnostics.exportExplainBundle": return "Export Diagnostics Explain Bundle";
    case "vitte.diagnostics.goToFirstErrorInFile": return "First Error In File";
    default: return command.replace(/^vitte\./, "");
  }
}

function applyStatusBar(): void {
  if (!statusItem) return;
  let text: string;
  let tooltipParts: string[];

  if (statusOverrideText !== undefined) {
    text = statusOverrideText;
    tooltipParts = [statusOverrideTooltip ?? statusBaseTooltip];
  } else {
    const suffix = statusLanguageSuffix ? ` (${statusLanguageSuffix})` : "";
    text = `${statusBaseIcon} ${STATUS_LABEL}${suffix}`;
    tooltipParts = [statusBaseTooltip];
    if (statusOverrideTooltip) {
      tooltipParts.push(statusOverrideTooltip);
    }
  }

  if (statusHealthIcon) {
    text = `${text} ${statusHealthIcon}`;
  }
  if (suggestionEngineModeSuffix) {
    text = `${text} [${suggestionEngineModeSuffix}]`;
  }
  if (syntaxParsingInFlight > 0) {
    text = `${text} $(sync~spin)`;
    tooltipParts.push(`Parsing… (${syntaxParsingInFlight})`);
  }

  if (statusHealthTooltip) {
    tooltipParts.push(statusHealthTooltip);
  }

  statusItem.text = text;
  statusItem.tooltip = tooltipParts.filter(Boolean).join("\n");
  statusItem.accessibilityInformation = {
    label: text.replace(/\$\([^)]+\)/g, "").trim(),
    role: "status"
  };
  if (offlineReason) {
    statusItem.color = OFFLINE_STATUS_COLOR;
    statusItem.backgroundColor = OFFLINE_STATUS_BG;
  } else {
    statusItem.color = undefined;
    statusItem.backgroundColor = undefined;
  }
}

function setStatusBase(icon: string, tooltip: string): void {
  statusBaseIcon = icon;
  statusBaseTooltip = tooltip;
  statusOverrideText = undefined;
  statusOverrideTooltip = undefined;
  applyStatusBar();
}

function setStatusLanguageSuffix(lang?: string): void {
  statusLanguageSuffix = lang ?? "";
  applyStatusBar();
}

function setStatusOverride(text?: string, tooltip?: string): void {
  statusOverrideText = text;
  statusOverrideTooltip = tooltip;
  applyStatusBar();
}

function isOfflineEnabled(): boolean {
  return vscode.workspace.getConfiguration("vitte").get<boolean>("server.offline", false);
}

function shouldFormatOnSave(doc: vscode.TextDocument): boolean {
  if (!LANGUAGE_SET.has(doc.languageId)) return false;
  if (doc.isUntitled) return false;
  return vscode.workspace.getConfiguration("vitte").get<boolean>("format.onSave", false);
}

function isOfflinePermanent(): boolean {
  return vscode.workspace.getConfiguration("vitte").get<boolean>("server.offlinePermanent", false);
}

function isOfflineEffective(): boolean {
  return isOfflineEnabled() || isOfflinePermanent();
}

function setOfflineStatus(reason: string): void {
  offlineReason = reason;
  offlineSince = offlineSince ?? Date.now();
  setStatusBase("$(circle-slash)", "Vitte LSP: offline");
  const forced = isOfflinePermanent() ? "Offline permanent (user-forced)" : "Offline";
  const since = offlineSince ? ` since ${new Date(offlineSince).toLocaleTimeString()}` : "";
  setStatusOverride(`$(circle-slash) Vitte ${forced.toUpperCase()}`, `${reason}${since}`);
  void setServerOnlineContext(false);
  logOfflineReason(reason);
  void showOfflineBanner(reason);
  scheduleOfflineRetry();
  void vscode.commands.executeCommand("vitte.offline.refresh");
}

async function setServerOnlineContext(online: boolean): Promise<void> {
  try { await vscode.commands.executeCommand("setContext", "vitte.serverOnline", online); } catch { /* noop */ }
  try { await vscode.commands.executeCommand("setContext", "vitte.serverOffline", !online); } catch { /* noop */ }
}

function logOfflineReason(reason: string): void {
  try { output.appendLine(`[offline] ${reason}`); } catch { /* noop */ }
  void appendOfflineLog(reason);
}

async function showOfflineBanner(reason: string): Promise<void> {
  if (offlineBannerShown) return;
  const mute = vscode.workspace.getConfiguration("vitte").get<boolean>("server.offlineMuteBanner", false);
  if (mute) return;
  offlineBannerShown = true;
  const selection = await vscode.window.showWarningMessage(
    `Vitte server offline: ${reason}`,
    "Explain Offline",
    "Open Settings"
  );
  if (selection === "Explain Offline") {
    await vscode.commands.executeCommand("vitte.offline.explain");
  } else if (selection === "Open Settings") {
    void vscode.commands.executeCommand("workbench.action.openSettings", "vitte.server");
  }
}

async function setOfflineMode(enabled: boolean, reason?: string): Promise<void> {
  const config = vscode.workspace.getConfiguration("vitte");
  const hasWorkspace = Boolean(vscode.workspace.workspaceFolders?.length);
  const target = hasWorkspace ? vscode.ConfigurationTarget.Workspace : vscode.ConfigurationTarget.Global;
  await config.update("server.offline", enabled, target);
  if (enabled) {
    setOfflineStatus(reason ?? "Offline mode enabled.");
  } else {
    offlineReason = undefined;
    offlineBannerShown = false;
    offlineSince = undefined;
    cancelOfflineRetry();
    statusItem.color = undefined;
    statusItem.backgroundColor = undefined;
    setStatusBase("$(rocket)", "Vitte Language Server");
    await setServerOnlineContext(true);
  }
}

function scheduleOfflineRetry(): void {
  if (extensionShuttingDown) return;
  if (isOfflineEffective()) return;
  const cfg = vscode.workspace.getConfiguration("vitte");
  const enabled = cfg.get<boolean>("server.autoRetry", true);
  if (!enabled) return;
  const base = clampNumber(cfg.get<number>("server.autoRetryBaseMs", 60000), 10000, 300000, 60000);
  const max = clampNumber(cfg.get<number>("server.autoRetryMaxMs", 300000), base, 900000, 300000);
  if (!offlineRetryTimer) {
    offlineRetryMs = Math.max(base, offlineRetryMs);
    offlineRetryMs = Math.min(offlineRetryMs, max);
    offlineRetryTimer = setTimeout(() => {
      if (extensionShuttingDown) return;
      offlineRetryTimer = undefined;
      void (async () => {
        if (extensionShuttingDown) return;
        try {
          const ok = await requestClientRestart(lastActivationContext ?? undefined, "offline-retry");
          if (ok) {
            offlineRetryMs = base;
            cancelOfflineRetry();
            return;
          }
          throw new Error("restart failed");
        } catch {
          offlineRetryMs = Math.min(offlineRetryMs * 2, max);
          scheduleOfflineRetry();
        }
      })();
    }, offlineRetryMs);
  }
}

function cancelOfflineRetry(): void {
  if (offlineRetryTimer) clearTimeout(offlineRetryTimer);
  offlineRetryTimer = undefined;
  offlineRetryMs = 60000;
}

async function appendOfflineLog(reason: string): Promise<void> {
  try {
    const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    const base = folder ?? os.tmpdir();
    const dir = path.join(base, ".vitte");
    await fs.promises.mkdir(dir, { recursive: true });
    const file = path.join(dir, "offline.log");
    const line = `${new Date().toISOString()} ${reason}\n`;
    await fs.promises.appendFile(file, line, "utf8");
  } catch { /* noop */ }
}

function clampNumber(value: number, min: number, max: number, fallback: number): number {
  if (!Number.isFinite(value)) return fallback;
  return Math.min(max, Math.max(min, value));
}

function refreshDiagnosticsStatus(): void {
  const summary = summarizeWorkspaceDiagnostics();
  const level = diagnosticsLevel(summary);
  switch (level) {
    case "error":
      statusHealthIcon = "$(error)";
      break;
    case "warning":
      statusHealthIcon = "$(warning)";
      break;
    default:
      statusHealthIcon = "$(pass-filled)";
  }
  statusHealthTooltip = formatDiagnosticsSummary(summary);
  applyStatusBar();
}

function ensureFileWatchers(context: vscode.ExtensionContext): vscode.FileSystemWatcher[] {
  if (fileWatchers.length === 0) {
    fileWatchers = WATCH_PATTERNS.map((pattern) => {
      const watcher = vscode.workspace.createFileSystemWatcher(pattern);
      context.subscriptions.push(watcher);
      return watcher;
    });
  }
  return fileWatchers;
}

function updateCommandButtons(context: vscode.ExtensionContext): void {
  for (const item of commandButtonItems) {
    try { item.dispose(); } catch { /* noop */ }
  }
  commandButtonItems = [];
  if (commandMenuButton) {
    try { commandMenuButton.dispose(); } catch { /* noop */ }
    commandMenuButton = undefined;
  }
  const shortcuts = readCommandShortcuts();
  const visible = shortcuts.filter((entry) => entry.statusBar !== false);
  let priority = 1000;
  if (visible.length === 0) return;
  for (const shortcut of visible) {
    const icon = shortcut.icon ?? "$(rocket)";
    const item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, priority--);
    item.text = `${icon} ${shortcut.label}`;
    item.command = shortcut.command;
    item.tooltip = shortcut.tooltip ?? shortcut.command;
    item.name = `Vitte ${shortcut.label}`;
    item.accessibilityInformation = { label: `${shortcut.label} button`, role: "button" };
    item.show();
    commandButtonItems.push(item);
    context.subscriptions.push(item);
  }
  commandMenuButton = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, priority--);
  commandMenuButton.text = "$(list-selection) Vitte";
  commandMenuButton.command = "vitte.showCommandMenu";
  commandMenuButton.tooltip = "Show additional Vitte commands";
  commandMenuButton.name = "Vitte Command Menu";
  commandMenuButton.accessibilityInformation = { label: "Vitte command menu", role: "button" };
  commandMenuButton.show();
  context.subscriptions.push(commandMenuButton);
}

async function showStartupCommandPrompt(context: vscode.ExtensionContext): Promise<void> {
  if (process.env.VSCODE_TESTING === "1") return;
  const shortcuts = readCommandShortcuts().filter((entry) => entry.startup !== false);
  if (shortcuts.length === 0) return;
  const diagSummary = summarizeWorkspaceDiagnostics();
  const diagState = readDiagnosticsRefreshState(context);
  const recommendedCommands = new Set<string>();
  if (diagSummary.errors > 0) {
    recommendedCommands.add("vitte.test");
  }
  if (diagState.stale || diagSummary.warnings > 0) {
    recommendedCommands.add("vitte.diagnostics.refresh");
  }
  const items: CommandMessageItem[] = [];
  const seen = new Set<string>();
  const ordered = [...shortcuts].sort((a, b) => {
    const aRec = recommendedCommands.has(a.command);
    const bRec = recommendedCommands.has(b.command);
    if (aRec && !bRec) return -1;
    if (!aRec && bRec) return 1;
    return a.label.localeCompare(b.label);
  });
  for (const shortcut of ordered) {
    if (seen.has(shortcut.command)) continue;
    const recommended = recommendedCommands.has(shortcut.command);
    items.push({
      title: recommended ? `⭐ ${shortcut.label}` : shortcut.label,
      command: shortcut.command,
    });
    seen.add(shortcut.command);
  }
  for (const command of recommendedCommands) {
    if (seen.has(command)) continue;
    items.unshift({
      title: `⭐ ${labelForCommand(command)}`,
      command,
    });
    seen.add(command);
  }
  if (items.length === 0) return;

  const moreItem: CommandMessageItem = { title: "More…" };
  const dismissItem: CommandMessageItem = { title: "Dismiss", isCloseAffordance: true };
  const selection = await vscode.window.showInformationMessage<CommandMessageItem>(
    "Vitte is ready — run a command:",
    ...items,
    moreItem,
    dismissItem
  );
  if (!selection || selection === dismissItem) return;
  if (selection === moreItem) {
    await vscode.commands.executeCommand("vitte.showCommandMenu");
    return;
  }
  if (selection.command) {
    await vscode.commands.executeCommand(selection.command);
  }
}
export async function activate(context: vscode.ExtensionContext): Promise<ExtensionApi | undefined> {
  extensionShuttingDown = false;
  activationStartedAt = Date.now();
  lastActivationContext = context;
  try {
    output = vscode.window.createOutputChannel("Vitte Language Server", { log: true });
  } catch {
    output = vscode.window.createOutputChannel("Vitte Language Server");
  }
  output.appendLine("[activate] begin");
  output.appendLine("[activate] Vitte extension activated");
  obsLog("extension.activate.begin", "info", { extensionVersion: getExtensionVersion(context.extension) });
  try {
    statusItem = vscode.window.createStatusBarItem("vitte.status", vscode.StatusBarAlignment.Right, 100);
  } catch {
    statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  }
  statusItem.name = "Vitte LSP";
  statusItem.command = "vitte.showServerLog";
  context.subscriptions.push(output, statusItem);
  if (process.env.VSCODE_TESTING === "1") {
    ensureTestingWorkspaceFolder(context);
  }
  setStatusBase("$(rocket)", "Vitte Language Server");
  refreshDiagnosticsStatus();
  statusItem.show();
  reportSettingsIssues(output, "activate");
  localInlineEngine = new LocalInlineEngine(readLocalInlineEngineOptions());
  await localInlineEngine.initialize(context);
  aiInlinePipeline = new AiInlinePipeline(readAiInlinePipelineOptions());
  await aiInlinePipeline.initialize();
  void ensureCloudOptInIfNeeded();
  updateSuggestionEngineModeSuffix();
  updateCommandButtons(context);
  void showStartupCommandPrompt(context);
  void setServerOnlineContext(false);

  // Register command shortcuts early so they're available even if later init fails.
  context.subscriptions.push(
    vscode.commands.registerCommand('vitte.openPlayground', () => PlaygroundPanel.createOrShow(context)),
    vscode.commands.registerCommand("vitte.debugActivationStatus", async () => {
      const ext =
        vscode.extensions.getExtension("vittestudio.vitte-studio")
        ?? vscode.extensions.getExtension("VitteStudio.vitte-studio");
      const clientState = client ? ClientState[client.state] : "none";
      const details = [
        `id=${ext?.id ?? "unknown"}`,
        `version=${getExtensionVersion(ext)}`,
        `isActive=${String(ext?.isActive ?? false)}`,
        `clientState=${clientState}`,
      ].join("\n");
      output.appendLine(`[activate-debug]\n${details}`);
      output.show(true);
      await vscode.window.showInformationMessage(`Vitte activation status:\n${details}`);
    })
  );

  void startClient(context).catch((err) => {
    const message = err instanceof Error ? err.message : String(err);
    output.appendLine(`[activate] startClient failed: ${message}`);
  });
  if (isOfflineEffective()) {
    void showOfflineBanner(offlineReason ?? (isOfflinePermanent()
      ? "Offline permanent (user-forced)."
      : "Offline mode is enabled (vitte.server.offline)."));
  }

  editorLintCollection = vscode.languages.createDiagnosticCollection("vitte-lint");
  syntaxLintCollection = vscode.languages.createDiagnosticCollection("vitte-syntax");
  context.subscriptions.push(editorLintCollection);
  context.subscriptions.push(syntaxLintCollection);
  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument((doc) => {
    updateEditorLint(doc);
    scheduleLiveSyntaxDiagnostics(doc, "open");
    completionOpenedAtByDoc.set(doc.uri.toString(), Date.now());
    scheduleAstCacheInvalidation(doc, false);
    scheduleLocalInlineEngineUpdate(doc);
  }));
  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((e) => {
    const docKey = e.document.uri.toString();
    const now = Date.now();
    const prevTyping = typingSpeedByDoc.get(docKey);
    if (prevTyping) {
      const dt = Math.max(1, now - prevTyping.lastEditAt);
      const ewma = prevTyping.intervalEwmaMs * 0.75 + dt * 0.25;
      typingSpeedByDoc.set(docKey, { lastEditAt: now, intervalEwmaMs: ewma });
    } else {
      typingSpeedByDoc.set(docKey, { lastEditAt: now, intervalEwmaMs: 180 });
    }
    updateEditorLint(e.document);
    scheduleLiveSyntaxDiagnostics(e.document, "change");
    scheduleAstCacheInvalidation(e.document, true);
    scheduleLocalInlineEngineUpdate(e.document);
    const cand = inlineTopCandidateByDoc.get(docKey);
    if (cand && e.contentChanges.length > 0) {
      const ch = e.contentChanges[0];
      if (ch && ch.range.start.line === cand.line && ch.range.start.character === cand.character && ch.text.length > 0) {
        const normalizedCandidate = cand.text.replace(/\r/g, "");
        if (normalizedCandidate.startsWith(ch.text) || ch.text.startsWith(normalizedCandidate.slice(0, Math.min(normalizedCandidate.length, ch.text.length)))) {
          localInlineEngine?.noteAccepted(cand.left, normalizedCandidate);
          aiInlinePipeline?.noteAccepted();
        }
      }
      if ((Date.now() - cand.at) > 5000 || e.contentChanges.length > 1) {
        inlineTopCandidateByDoc.delete(docKey);
      }
    }
  }));
  context.subscriptions.push(vscode.workspace.onDidCloseTextDocument((doc) => {
    editorLintCollection?.delete(doc.uri);
    syntaxLintCollection?.delete(doc.uri);
    const key = doc.uri.toString();
    const syntaxTimer = syntaxLintTimers.get(key);
    if (syntaxTimer) {
      clearTimeout(syntaxTimer);
      syntaxLintTimers.delete(key);
    }
    const syntaxProc = syntaxLintProcByDoc.get(key);
    if (syntaxProc && !syntaxProc.killed) {
      try { syntaxProc.kill(); } catch { /* noop */ }
    }
    syntaxLintProcByDoc.delete(key);
    syntaxLintSeq.delete(key);
    completionOpenedAtByDoc.delete(key);
    typingSpeedByDoc.delete(key);
    completionStickyAcceptedByDoc.delete(key);
    inlineTopCandidateByDoc.delete(key);
    invalidateCompletionCachesForDocument(doc);
    completionAstFingerprintByDoc.delete(key);
    const t = completionAstRefreshTimers.get(key);
    if (t) {
      clearTimeout(t);
      completionAstRefreshTimers.delete(key);
    }
    localInlineEngine?.removeDocument(doc.uri);
    aiInlinePipeline?.remove(doc.uri.toString());
    const reindex = localInlineReindexTimers.get(key);
    if (reindex) {
      clearTimeout(reindex);
      localInlineReindexTimers.delete(key);
    }
  }));
  for (const doc of vscode.workspace.textDocuments) {
    updateEditorLint(doc);
    scheduleLiveSyntaxDiagnostics(doc, "open");
    completionOpenedAtByDoc.set(doc.uri.toString(), Date.now());
    scheduleAstCacheInvalidation(doc, false);
    scheduleLocalInlineEngineUpdate(doc);
  }

  // Debug & runtime tooling
  registerDebugConfigurationProvider(context);
  registerDebugFactory(context);
  registerRuntimeLocatorCommand(context);
  registerBuildTasks(context);
  registerBenchTasks(context);
  registerQuickActions(context);
  registerProjectAssistant(context, output, () => client, () => Date.now() - activationStartedAt);
  registerAdvancedCodeActions(context);
  registerVitteCodeLens(context);
  registerEnterpriseSuite(context, {
    output,
    getClient: () => client,
    getCrashCount: () => recentStops.length,
  });
  try {
    await registerTelemetry(context);
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    output.appendLine(`[activate] telemetry init failed: ${message}`);
  }

  // Commandes
  context.subscriptions.push(
    vscode.commands.registerCommand("vitte.showServerLog", () => {
      output.show(true);
    }),
    vscode.commands.registerCommand("vitte.showServerMetrics", async () => {
      const requestId = nextRequestId("metrics");
      obsLog("command.showServerMetrics.start", "info", undefined, requestId);
      if (isOfflineEffective()) return showOfflineNoop("metrics");
      if (!client) {
        const reason = offlineReason ? ` (${offlineReason})` : "";
        void vscode.window.showWarningMessage(`Vitte server is not running.${reason}`);
        obsLog("command.showServerMetrics.skipped", "warn", { reason: "client_not_running", offlineReason }, requestId);
        return;
      }
      try {
        const stats = await client.sendRequest<ServerMetricEntry[]>("vitte/metrics");
        if (!stats || stats.length === 0) {
          void vscode.window.showInformationMessage("Vitte: no metrics available yet.");
          obsLog("command.showServerMetrics.empty", "info", undefined, requestId);
          return;
        }
        const timestamp = new Date().toLocaleTimeString();
        output.appendLine(`[metrics] Snapshot ${timestamp}`);
        for (const entry of stats) {
          const avg = entry.averageMs.toFixed(2);
          const last = entry.lastMs.toFixed(2);
          const max = entry.maxMs.toFixed(2);
          const p99 = typeof entry.p99Ms === "number" ? entry.p99Ms.toFixed(2) : null;
          const when = entry.lastAt ? new Date(entry.lastAt).toLocaleTimeString() : "n/a";
          const countInfo = typeof entry.lastCount === "number" ? ` n=${entry.lastCount}` : "";
          const errInfo = entry.errorCount ? ` errors=${entry.errorCount}` : "";
          const p99Info = p99 ? ` p99=${p99}ms` : "";
          const lastErr = entry.lastError ? ` lastErr="${entry.lastError}"` : "";
          output.appendLine(
            `  ${entry.name.padEnd(18)} avg=${avg}ms last=${last}ms max=${max}ms${p99Info} count=${entry.count}${countInfo}${errInfo} last=${when} uri=${entry.lastUri}${lastErr}`
          );
        }
        output.show(true);
        obsLog("command.showServerMetrics.done", "info", { metricsCount: stats.length }, requestId);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Vitte: unable to fetch server metrics (${message})`);
        obsLog("command.showServerMetrics.failed", "error", { message }, requestId);
      }
    }),
    vscode.commands.registerCommand("vitte.metrics.reset", async () => {
      if (isOfflineEffective()) return showOfflineNoop("metrics reset");
      if (!client) {
        void vscode.window.showWarningMessage("Vitte server is not running.");
        return;
      }
      try {
        await client.sendRequest("vitte/metrics.reset");
        void vscode.window.showInformationMessage("Vitte: metrics reset.");
        void vscode.commands.executeCommand("vitte.metrics.refresh");
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Vitte: unable to reset metrics (${message})`);
      }
    }),
    vscode.commands.registerCommand("vitte.exportPerfSession", async () => {
      if (!client || client.state !== ClientState.Running) {
        void vscode.window.showWarningMessage("Vitte server is not running.");
        return;
      }
      try {
        const stats = await client.sendRequest<ServerMetricEntry[]>("vitte/metrics");
        const by = new Map(stats.map((s) => [s.name, s]));
        const mem = process.memoryUsage();
        const payload = {
          ts: new Date().toISOString(),
          activationMs: Date.now() - activationStartedAt,
          rssMB: Number((mem.rss / (1024 * 1024)).toFixed(2)),
          completionP95Ms: by.get("completion")?.p95Ms ?? null,
          hoverP95Ms: by.get("hover")?.p95Ms ?? null,
          renameP95Ms: by.get("rename")?.p95Ms ?? null,
          metrics: stats,
        };
        const dir = diagnosticsExportDir();
        if (!dir) return;
        await fs.promises.mkdir(dir, { recursive: true });
        const file = path.join(dir, "perf-session.json");
        await fs.promises.writeFile(file, `${JSON.stringify(payload, null, 2)}\n`, "utf8");
        void vscode.window.showInformationMessage(`Perf session exported: ${file}`);
      } catch (err) {
        void vscode.window.showErrorMessage(`Export perf session failed: ${String(err)}`);
      }
    }),
    vscode.commands.registerCommand("vitte.pingServer", async () => {
      const requestId = nextRequestId("ping");
      obsLog("command.pingServer.start", "info", undefined, requestId);
      if (isOfflineEffective()) return showOfflineNoop("ping");
      if (!client) {
        void vscode.window.showWarningMessage("Vitte server is not running.");
        obsLog("command.pingServer.skipped", "warn", { reason: "client_not_running" }, requestId);
        return;
      }
      try {
        const res = await client.sendRequest<{ ok: boolean; ts: number }>("vitte/ping");
        void vscode.window.showInformationMessage(`Vitte: pong (${res.ok ? "ok" : "fail"}) at ${new Date(res.ts).toLocaleTimeString()}`);
        obsLog("command.pingServer.done", "info", { ok: res.ok }, requestId);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Vitte: ping failed (${message})`);
        obsLog("command.pingServer.failed", "error", { message }, requestId);
      }
    }),
    vscode.commands.registerCommand("vitte.observability.export", async () => {
      const requestId = nextRequestId("obs-export");
      obsLog("command.observability.export.start", "info", undefined, requestId);
      try {
        const dir = diagnosticsExportDir();
        if (!dir) return;
        await fs.promises.mkdir(dir, { recursive: true });
        let metrics: ServerMetricEntry[] | undefined;
        if (client && client.state === ClientState.Running) {
          try {
            metrics = await client.sendRequest<ServerMetricEntry[]>("vitte/metrics");
          } catch {
            // ignore metrics fetch errors during export
          }
        }
        const payload = {
          ts: new Date().toISOString(),
          activationMs: Date.now() - activationStartedAt,
          clientState: client ? ClientState[client.state] : "none",
          healthFailures,
          reliabilityAttempts,
          recentStopCount: recentStops.length,
          diagnosticHelp: diagnosticHelpObservabilitySnapshot(),
          events: [...obsHistory],
          metrics: metrics ?? null,
        };
        const file = path.join(dir, "observability-session.json");
        await fs.promises.writeFile(file, `${JSON.stringify(payload, null, 2)}\n`, "utf8");
        void vscode.window.showInformationMessage(`Observability session exported: ${file}`);
        obsLog("command.observability.export.done", "info", { file }, requestId);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Observability export failed: ${message}`);
        obsLog("command.observability.export.failed", "error", { message }, requestId);
      }
    }),
    vscode.commands.registerCommand("vitte.showCommandMenu", async () => {
      const pickItems: CommandMenuItem[] = COMMAND_MENU_ENTRIES.map((entry) => {
        const item: CommandMenuItem = {
          label: entry.label,
          command: entry.command,
          detail: entry.detail ?? entry.command,
        };
        if (entry.description) {
          item.description = entry.description;
        }
        return item;
      });
      const pick = await vscode.window.showQuickPick<CommandMenuItem>(pickItems, {
        title: "Vitte commands",
        placeHolder: "Select a command to run",
        matchOnDetail: true,
      });
      if (!pick) return;
      await vscode.commands.executeCommand(pick.command);
    }),
    vscode.commands.registerCommand("vitte.suggestions.loadNextPage", async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) return;
      if (!isVitteDocument(editor.document)) {
        void vscode.window.showWarningMessage("Vitte suggestions pagination is available only in Vitte/Vit files.");
        return;
      }
      const cfg = vscode.workspace.getConfiguration("vitte");
      const maxPages = Math.max(1, Math.min(10, cfg.get<number>("suggestions.maxPages", 5)));
      const key = getCompletionRequestKey(editor.document, editor.selection.active);
      const current = completionPagingState.get(key)?.page ?? 1;
      const nextPage = Math.min(maxPages, current + 1);
      if (nextPage === current) {
        vscode.window.setStatusBarMessage(`Vitte suggestions page: ${nextPage}/${maxPages}`, 1200);
        return;
      }
      completionPagingState.set(key, { page: nextPage, ts: Date.now() });
      completionLoadNextPageKey = key;
      completionLoadNextPageIssuedAt = Date.now();
      await vscode.commands.executeCommand("editor.action.triggerSuggest");
      vscode.window.setStatusBarMessage(`Vitte suggestions page: ${nextPage}/${maxPages}`, 1800);
    }),
    vscode.commands.registerCommand("vitte.inline.nextSuggestion", async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || !isVitteDocument(editor.document)) return;
      const key = `${editor.document.uri.toString()}::${editor.selection.active.line}:${editor.selection.active.character}`;
      const current = inlineSuggestionCycleByKey.get(key) ?? 0;
      inlineSuggestionCycleByKey.set(key, current + 1);
      await vscode.commands.executeCommand("editor.action.inlineSuggest.hide");
      await vscode.commands.executeCommand("editor.action.inlineSuggest.trigger");
      vscode.window.setStatusBarMessage("Vitte inline: next suggestion", 900);
    }),
    vscode.commands.registerCommand("vitte.inline.nextEdit", async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || !isVitteDocument(editor.document)) return;
      inlineNextEditBoostByDoc.set(editor.document.uri.toString(), Date.now() + 5000);
      await vscode.commands.executeCommand("editor.action.inlineSuggest.trigger");
      vscode.window.setStatusBarMessage("Vitte inline: next edit", 1200);
    }),
    vscode.commands.registerCommand("vitte.inline.toggle", async () => {
      const cfg = vscode.workspace.getConfiguration("vitte");
      const current = cfg.get<boolean>("suggestions.ghostTextTop1Stable", true);
      await cfg.update("suggestions.ghostTextTop1Stable", !current, vscode.ConfigurationTarget.Workspace);
      updateSuggestionEngineModeSuffix();
      void vscode.window.showInformationMessage(`Vitte inline suggestions: ${!current ? "enabled" : "disabled"}.`);
    }),
    vscode.commands.registerCommand("vitte.suggestions.refreshContext", async () => {
      await aiInlinePipeline?.refreshWorkspaceContext();
      for (const doc of vscode.workspace.textDocuments) {
        if (!isVitteDocument(doc)) continue;
        void aiInlinePipeline?.upsert(doc.uri.toString(), doc.getText());
      }
      void vscode.window.showInformationMessage("Vitte suggestions context refreshed.");
    }),
    vscode.commands.registerCommand("vitte.suggestions.cloudOptIn", async () => {
      const cfg = vscode.workspace.getConfiguration("vitte");
      await cfg.update("suggestions.aiPipeline.cloudOptIn", true, vscode.ConfigurationTarget.Workspace);
      await cfg.update("suggestions.aiPipeline.localOnly", false, vscode.ConfigurationTarget.Workspace);
      updateSuggestionEngineModeSuffix();
      void vscode.window.showInformationMessage("Vitte AI cloud opt-in enabled.");
    }),
    vscode.commands.registerCommand("vitte.suggestions.cloudOptOut", async () => {
      const cfg = vscode.workspace.getConfiguration("vitte");
      await cfg.update("suggestions.aiPipeline.cloudOptIn", false, vscode.ConfigurationTarget.Workspace);
      await cfg.update("suggestions.aiPipeline.localOnly", true, vscode.ConfigurationTarget.Workspace);
      updateSuggestionEngineModeSuffix();
      void vscode.window.showInformationMessage("Vitte switched to local-only mode.");
    }),
    vscode.commands.registerCommand("vitte.suggestions.resetLearning", async () => {
      resetSuggestionLearningState();
      resetSuggestionRuntimeState();
      await vscode.commands.executeCommand("vitte.metrics.refresh");
      void vscode.window.showInformationMessage("Vitte suggestions: local learning state reset.");
    }),
    vscode.commands.registerCommand("vitte.suggestions.exportDiagnostics", async () => {
      try {
        const workspaceDir = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        if (!workspaceDir) {
          void vscode.window.showWarningMessage("Vitte: open a workspace to export suggestion diagnostics.");
          return;
        }
        const cfg = vscode.workspace.getConfiguration("vitte");
        const payload = {
          ts: new Date().toISOString(),
          workspace: workspaceDir,
          streaming: getStreamingCompletionStats(),
          localInlineEngine: localInlineEngine?.getStats() ?? null,
          aiInlinePipeline: aiInlinePipeline?.getTelemetry() ?? null,
          profilerRecent: getSuggestionTraceHistory(120),
          learning: getSuggestionLearningSnapshot(),
          config: {
            intentMode: cfg.get<string>("suggestions.intentMode"),
            topN: cfg.get<number>("suggestions.topN"),
            minN: cfg.get<number>("suggestions.minN"),
            maxN: cfg.get<number>("suggestions.maxN"),
            pageSize: cfg.get<number>("suggestions.pageSize"),
            maxPages: cfg.get<number>("suggestions.maxPages"),
            lspHardTimeoutMs: cfg.get<number>("suggestions.lspHardTimeoutMs"),
            rankingDeadlineMs: cfg.get<number>("suggestions.rankingDeadlineMs"),
          },
        };
        const dir = diagnosticsExportDir();
        if (!dir) {
          void vscode.window.showWarningMessage("Vitte: unable to resolve diagnostics export directory.");
          return;
        }
        await fs.promises.mkdir(dir, { recursive: true });
        const file = path.join(dir, "suggestions-diagnostics.json");
        await fs.promises.writeFile(file, `${JSON.stringify(payload, null, 2)}\n`, "utf8");
        void vscode.window.showInformationMessage(`Vitte suggestions diagnostics exported: ${file}`);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Vitte: failed to export suggestions diagnostics (${message})`);
      }
    }),
    vscode.commands.registerCommand("vitte.suggestions.showLocalEngineStats", () => {
      const s = localInlineEngine?.getStats();
      if (!s) {
        void vscode.window.showInformationMessage("Vitte local inline engine: not initialized.");
        return;
      }
      const msg = [
        `requests=${s.requests}`,
        `cacheHitRate=${(s.cacheHitRate * 100).toFixed(1)}%`,
        `p50=${s.latencyP50Ms}ms`,
        `p95=${s.latencyP95Ms}ms`,
        `indexedFiles=${s.indexedFiles}`,
        `learnedContexts=${s.acceptanceContexts}`,
        `learnedEntries=${s.acceptanceEntries}`,
        `hotCacheEntries=${s.hotCacheEntries}`,
      ].join(" | ");
      void vscode.window.showInformationMessage(`Vitte local engine stats: ${msg}`);
    }),
    vscode.commands.registerCommand("vitte.suggestions.profileResources", () => {
      const ai = aiInlinePipeline?.getTelemetry();
      const mem = process.memoryUsage();
      if (!ai) {
        void vscode.window.showInformationMessage("Vitte AI pipeline profiler: unavailable.");
        return;
      }
      const msg = [
        `aiReq=${ai.requests}`,
        `aiAvg/P95=${ai.avgLatencyMs}/${ai.p95LatencyMs}ms`,
        `cpuAvg/P95=${ai.avgCpuMicros}/${ai.p95CpuMicros}us`,
        `memDeltaAvg/P95=${ai.avgMemDeltaKb}/${ai.p95MemDeltaKb}KB`,
        `rss=${Math.round(mem.rss / (1024 * 1024))}MB`,
      ].join(" | ");
      void vscode.window.showInformationMessage(`Vitte suggestions resources: ${msg}`);
    }),
    vscode.commands.registerCommand("vitte.suggestions.openProfiler", () => {
      openSuggestionProfilerPanel(context);
    }),
    vscode.commands.registerCommand("vitte.diagnostics.exportSnapshot", async () => {
      try {
        const workspaceDir = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        if (!workspaceDir) {
          void vscode.window.showWarningMessage("Vitte: open a workspace to export diagnostics snapshot.");
          return;
        }
        const report = buildWorkspaceDiagnosticsReport();
        const payload = {
          ts: new Date().toISOString(),
          workspace: workspaceDir,
          summary: summarizeWorkspaceDiagnostics(),
          diagnosticHelp: diagnosticHelpObservabilitySnapshot(),
          perFile: report.perFile,
          perDirectory: summarizeDiagnosticsByDirectory(),
        };
        const dir = diagnosticsExportDir();
        if (!dir) {
          void vscode.window.showWarningMessage("Vitte: unable to resolve diagnostics export directory.");
          return;
        }
        await fs.promises.mkdir(dir, { recursive: true });
        const file = path.join(dir, `diagnostics-snapshot-${Date.now()}.json`);
        await fs.promises.writeFile(file, `${JSON.stringify(payload, null, 2)}\n`, "utf8");
        void vscode.window.showInformationMessage(`Vitte diagnostics snapshot exported: ${file}`);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Vitte: failed to export diagnostics snapshot (${message})`);
      }
    }),
    vscode.commands.registerCommand("vitte.diagnostics.exportExplainBundle", async () => {
      try {
        const workspaceDir = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        if (!workspaceDir) {
          void vscode.window.showWarningMessage("Vitte: open a workspace to export diagnostics explain bundle.");
          return;
        }
        const report = buildWorkspaceDiagnosticsReport();
        const cfg = vscode.workspace.getConfiguration("vitte");
        const helpCachePath = vitteExplainHelpCachePath(workspaceDir);
        const payload = {
          ts: new Date().toISOString(),
          workspace: workspaceDir,
          schema: "diagnostics_explain_bundle@1",
          summary: summarizeWorkspaceDiagnostics(),
          diagnosticHelp: diagnosticHelpObservabilitySnapshot(),
          settings: {
            helpSource: cfg.get<DiagnosticHelpSource>("diagnostics.helpSource", "auto"),
            explainTimeoutMs: cfg.get<number>("diagnostics.explainTimeoutMs", 450),
            lang: cfg.get<string>("lang", "en"),
          },
          helpCache: {
            path: helpCachePath,
            exists: fs.existsSync(helpCachePath),
            inMemoryEntries: vitteExplainHelpCache.size,
          },
          perFile: report.perFile,
          perDirectory: summarizeDiagnosticsByDirectory(),
        };
        const dir = diagnosticsExportDir();
        if (!dir) {
          void vscode.window.showWarningMessage("Vitte: unable to resolve diagnostics export directory.");
          return;
        }
        await fs.promises.mkdir(dir, { recursive: true });
        const file = path.join(dir, `diagnostics-explain-bundle-${Date.now()}.json`);
        await fs.promises.writeFile(file, `${JSON.stringify(payload, null, 2)}\n`, "utf8");
        void vscode.window.showInformationMessage(`Vitte diagnostics explain bundle exported: ${file}`);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`Vitte: failed to export diagnostics explain bundle (${message})`);
      }
    }),
    vscode.commands.registerCommand("vitte.diagnostics.refreshHelpCache", () => {
      const result = clearVitteExplainHelpCache(vscode.workspace.workspaceFolders);
      for (const doc of vscode.workspace.textDocuments) {
        if (!isVitteDocument(doc)) continue;
        scheduleLiveSyntaxDiagnostics(doc, "config");
      }
      const detail = result.failedFiles > 0
        ? ` (${result.deletedFiles} file(s) deleted, ${result.failedFiles} failed)`
        : ` (${result.deletedFiles} file(s) deleted)`;
      void vscode.window.showInformationMessage(`Vitte diagnostics help cache refreshed${detail}.`);
    }),
    vscode.commands.registerCommand("vitte.diagnostics.goToFirstErrorInFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) {
        void vscode.window.showInformationMessage("Vitte: no active editor.");
        return;
      }
      const uri = editor.document.uri;
      const diagnostics = vscode.languages.getDiagnostics(uri)
        .filter((d) => d.severity === vscode.DiagnosticSeverity.Error)
        .sort((a, b) => {
          const lineDiff = a.range.start.line - b.range.start.line;
          if (lineDiff !== 0) return lineDiff;
          return a.range.start.character - b.range.start.character;
        });
      const first = diagnostics[0];
      if (!first) {
        void vscode.window.showInformationMessage("Vitte: no error in active file.");
        return;
      }
      editor.revealRange(first.range, vscode.TextEditorRevealType.InCenter);
      editor.selection = new vscode.Selection(first.range.start, first.range.start);
    }),
    vscode.commands.registerCommand("vitte.test.renderDiagnosticMessage", (rawCode?: unknown, baseMessage?: unknown, options?: unknown) => {
      const cfg = vscode.workspace.getConfiguration("vitte");
      const lang = cfg.get<string>("lang", "en");
      const helpSource = cfg.get<DiagnosticHelpSource>("diagnostics.helpSource", "auto");
      const explainTimeoutMs = Math.max(100, Math.min(5000, cfg.get<number>("diagnostics.explainTimeoutMs", 450)));
      const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? process.cwd();
      const forcedBin = (options && typeof options === "object" && "bin" in options)
        ? (options as { bin?: unknown }).bin
        : undefined;
      const bin = typeof forcedBin === "string" && forcedBin.trim().length > 0 ? forcedBin.trim() : "vitte";
      const resolved = { bin, cwd: folder };
      const rawCodeText = typeof rawCode === "string" || typeof rawCode === "number" ? String(rawCode) : "";
      const baseMessageText = typeof baseMessage === "string" && baseMessage.length > 0 ? baseMessage : "Syntax error";
      const code = normalizeDiagCode(rawCodeText);
      const baseCode = explainableDiagCode(code) ?? code;
      const explain = baseCode ? resolveDiagnosticHelp(baseCode, lang, resolved, helpSource, explainTimeoutMs) : undefined;
      return baseCode ? formatVitteDiagnosticMessage(baseMessageText, baseCode, explain) : baseMessageText;
    }),
    vscode.commands.registerCommand("vitte.test.getDiagnosticHelpObservability", () => diagnosticHelpObservabilitySnapshot()),
    vscode.commands.registerCommand("vitte.restartServer", async () => {
      if (isOfflinePermanent()) {
        return showOfflineNoop("restart");
      }
      await vscode.window.withProgress(
        {
          location: vscode.ProgressLocation.Notification,
          title: "Vitte: restarting language server…",
        },
        async () => {
          const ok = await requestClientRestart(context, "command");
          if (!ok) {
            showOfflineNoop("restart");
          }
        }
      );
      vscode.window.setStatusBarMessage("Vitte server restart attempted.", 3000);
    }),
    vscode.commands.registerCommand("vitte.runAction", async () => {
      const pick = await vscode.window.showQuickPick([
        {
          label: "Format document",
          description: "editor.action.formatDocument",
          detail: "Apply the configured formatter to the active file.",
          action: "format",
        },
        {
          label: "Organize imports",
          description: "editor.action.organizeImports",
          detail: "Trie et nettoie les imports du document courant.",
          action: "organizeImports",
        },
        {
          label: "Fix all",
          description: "source.fixAll",
          detail: "Run available quick fixes.",
          action: "fixAll",
        }
      ], { title: "Vitte: run a quick action" });
      if (!pick) return;
      await runBuiltinAction(pick.action);
    }),
    vscode.commands.registerCommand("vitte.runActionWithArgs", async () => {
      const action = await vscode.window.showInputBox({ prompt: "Action (format | organizeImports | fixAll)", value: "format" });
      if (!action) return;
      await runBuiltinAction(action.trim());
    }),
    vscode.commands.registerCommand("vitte.formatDocument", async () => runBuiltinAction("format")),
    vscode.commands.registerCommand("vitte.organizeImports", async () => runBuiltinAction("organizeImports")),
    vscode.commands.registerCommand("vitte.fixAll", async () => runBuiltinAction("fixAll")),
    vscode.commands.registerCommand("vitte.renameSymbol", async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) return;
      const doc = editor.document;
      const pos = editor.selection.active;
      const oldName = doc.getText(doc.getWordRangeAtPosition(pos) ?? new vscode.Range(pos, pos)) || "symbol";
      const newName = await vscode.window.showInputBox({
        prompt: `Rename '${oldName}' to`,
        value: oldName,
        ignoreFocusOut: true,
      });
      if (!newName || newName === oldName) return;

      const renameEdit = await vscode.commands.executeCommand<vscode.WorkspaceEdit>(
        "vscode.executeDocumentRenameProvider",
        doc.uri,
        pos,
        newName
      );
      if (!renameEdit) return;

      const conflicts = await detectRenameConflicts(renameEdit, newName);
      if (conflicts.length > 0) {
        const preview = conflicts.slice(0, 8).join(", ");
        void vscode.window.showErrorMessage(
          `Rename blocked: symbol '${newName}' already exists in ${conflicts.length} file(s): ${preview}${conflicts.length > 8 ? "…" : ""}`
        );
        return;
      }

      const preview = summarizeWorkspaceEditDetailed(renameEdit);
      const choice = await vscode.window.showInformationMessage(
        `Rename preview: ${preview.fileCount} file(s), ${preview.editCount} edit(s). ${preview.details.join(" | ")}`,
        "Apply",
        "Cancel"
      );
      if (choice !== "Apply") return;

      const snapshots = await captureWorkspaceEditSnapshots(renameEdit);
      try {
        const ok = await vscode.workspace.applyEdit(renameEdit);
        if (!ok) throw new Error("applyEdit returned false");
      } catch (err) {
        await rollbackWorkspaceEditSnapshots(snapshots);
        await writeRenameReport({
          ts: new Date().toISOString(),
          oldName,
          newName,
          fileCount: preview.fileCount,
          editCount: preview.editCount,
          applied: false,
          error: String(err),
          files: preview.details,
        });
        void vscode.window.showErrorMessage(`Vitte rename rollback: ${String(err)}`);
        return;
      }
      await writeRenameReport({
        ts: new Date().toISOString(),
        oldName,
        newName,
        fileCount: preview.fileCount,
        editCount: preview.editCount,
        applied: true,
        files: preview.details,
      });
    }),
    vscode.commands.registerCommand("vitte.applyEditSample", async () => {
      const editor = vscode.window.activeTextEditor; if (!editor) return;
      const edit = new vscode.WorkspaceEdit();
      edit.insert(editor.document.uri, new vscode.Position(0, 0), "// Edited by Vitte sample\n");
      await vscode.workspace.applyEdit(edit);
    }),
    vscode.commands.registerCommand("vitte.progressSample", async () => {
      await vscode.window.withProgress({ location: vscode.ProgressLocation.Window, title: "Vitte: Running task" }, async (p) => {
        p.report({ message: "Step 1/3" }); await sleep(250);
        p.report({ message: "Step 2/3" }); await sleep(250);
        p.report({ message: "Step 3/3" }); await sleep(250);
      });
    }),
    vscode.commands.registerCommand("vitte.showInfo", async () => {
      const cfg = vscode.workspace.getConfiguration("vitte");
      const trace = cfg.get<string>("trace.server", "off");
      const offline = cfg.get<boolean>("server.offline", false);
      const offlinePermanent = cfg.get<boolean>("server.offlinePermanent", false);
      const offlineMsg = offlinePermanent ? " (offline permanent)" : (offline ? " (offline)" : "");
      await vscode.window.showInformationMessage(`Vitte LSP — trace: ${trace}${offlineMsg}`);
    }),
    vscode.commands.registerCommand("vitte.offline.explain", async () => {
      const uri = vscode.Uri.file(path.join(context.extensionPath, "media", "offline.md"));
      await vscode.commands.executeCommand("vscode.open", uri);
      void vscode.commands.executeCommand("vitte.offline.openLog");
    }),
    vscode.commands.registerCommand("vitte.offline.copyReport", async () => {
      const report = await readOfflineReport();
      await vscode.env.clipboard.writeText(report);
      void vscode.window.showInformationMessage("Vitte: offline report copied to clipboard.");
    }),
    vscode.commands.registerCommand("vitte.goOffline", async () => {
      await setOfflineMode(true, "Manual offline mode enabled.");
      if (client) {
        try { await client.stop(); } catch { /* noop */ }
        client = undefined;
      }
    }),
    vscode.commands.registerCommand("vitte.debug.runFile", async () => { await runDebugCurrentFile(); }),
    vscode.commands.registerCommand("vitte.debug.attachServer", async () => { await attachDebugServer(); }),
  );
  await ensureBaselineContributedCommands(context);

  // Refresh status depending on the active editor
  context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor(updateStatusText));
  context.subscriptions.push(vscode.languages.registerInlineCompletionItemProvider(
    LANGUAGES.map((id) => ({ language: id, scheme: "file" })),
    {
      async provideInlineCompletionItems(
        document: vscode.TextDocument,
        position: vscode.Position,
        _ctx: vscode.InlineCompletionContext,
        _token: vscode.CancellationToken,
      ): Promise<vscode.InlineCompletionItem[] | vscode.InlineCompletionList> {
        const started = Date.now();
        const cfg = vscode.workspace.getConfiguration("vitte");
        const enabled = cfg.get<boolean>("suggestions.ghostTextTop1Stable", true);
        if (!enabled) return [];
        const cacheTtlMs = Math.max(50, Math.min(1000, cfg.get<number>("suggestions.inlineCacheTtlMs", INLINE_CACHE_TTL_MS)));
        const key = `${document.uri.toString()}::${position.line}:${position.character}`;
        const cached = inlineCompletionCache.get(key);
        if (cached && (Date.now() - cached.ts) <= cacheTtlMs) {
          return cached.items;
        }

        const threshold = Math.max(1, Math.min(8, cfg.get<number>("suggestions.ghostTextStableThreshold", 2)));
        const maxAgeMs = Math.max(200, Math.min(10000, cfg.get<number>("suggestions.ghostTextMaxAgeMs", 2500)));
        const requestKey = getCompletionRequestKey(document, position);
        const candidate = completionTop1StableByKey.get(requestKey);
        const items: vscode.InlineCompletionItem[] = [];
        const nextEditUntil = inlineNextEditBoostByDoc.get(document.uri.toString()) ?? 0;

        if (candidate && candidate.count >= threshold && (Date.now() - candidate.lastAt) <= maxAgeMs) {
          const wordRange = document.getWordRangeAtPosition(position);
          const prefix = wordRange ? document.getText(wordRange) : "";
          const lhs = candidate.text.toLowerCase();
          const rhs = prefix.toLowerCase();
          if (prefix.length === 0 || (lhs.startsWith(rhs) && lhs !== rhs)) {
            items.push(new vscode.InlineCompletionItem(candidate.text, wordRange ?? new vscode.Range(position, position)));
          }
        }

        if (Date.now() <= nextEditUntil) {
          const nextEdit = inlineNextEditCandidate(document, position);
          if (nextEdit) {
            items.unshift(new vscode.InlineCompletionItem(nextEdit, new vscode.Range(position, position)));
          }
        }

        const heuristic = inlineHeuristicCandidate(document, position);
        if (heuristic) {
          items.push(new vscode.InlineCompletionItem(heuristic, new vscode.Range(position, position)));
        }
        const localInlineTopK = Math.max(1, Math.min(24, cfg.get<number>("suggestions.localEngine.inlineTopK", 8)));
        const preferMultiline = Date.now() <= nextEditUntil;
        const localCandidates = localInlineEngine?.suggest(document, position, localInlineTopK, { preferMultiline }) ?? [];
        const aiCandidates = await (aiInlinePipeline?.suggest(document, position, localCandidates) ?? Promise.resolve(localCandidates));
        for (const suggestion of aiCandidates) {
          items.push(new vscode.InlineCompletionItem(suggestion, new vscode.Range(position, position)));
        }

        const deduped = dedupeInlineTexts(items).slice(0, 4);
        const cycle = inlineSuggestionCycleByKey.get(key) ?? 0;
        const finalItems = rotateInlineItems(deduped, cycle);
        if (finalItems.length > 0) {
          const top = finalItems[0];
          if (top) {
            const topText = typeof top.insertText === "string" ? top.insertText : top.insertText.value;
            const docKey = document.uri.toString();
            inlineTopCandidateByDoc.set(docKey, {
              line: position.line,
              character: position.character,
              left: document.lineAt(position.line).text.slice(0, position.character),
              text: topText,
              at: Date.now(),
            });
          }
        }
        inlineCompletionCache.set(key, { items: finalItems, ts: Date.now() });
        const elapsed = Date.now() - started;
        if (elapsed > 150) {
          obsLog("inline.latency.local.slow", "warn", { elapsedMs: elapsed, targetMs: 150 });
        }
        return finalItems;
      },
    },
  ));
  context.subscriptions.push(vscode.window.onDidChangeTextEditorSelection((e) => {
    scheduleCompletionIdlePrefetch(e.textEditor);
  }));
  scheduleCompletionIdlePrefetch(vscode.window.activeTextEditor ?? undefined);
  updateStatusText(vscode.window.activeTextEditor ?? undefined);

  // Relance si config Vitte change
  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((e) => {
    if (e.affectsConfiguration("vitte.commandShortcuts")) {
      updateCommandButtons(context);
    }
    if (e.affectsConfiguration("vitte")) {
      reportSettingsIssues(output, "config");
      if (shouldRestartForConfigChange(e)) {
        scheduleConfigRestart(context, "config-change");
      } else {
        obsLog("config.restart.skipped", "info");
      }
    }
    if (e.affectsConfiguration("vitte.server.offlinePermanent")) {
      if (isOfflinePermanent()) {
        setOfflineStatus("Offline permanent (user-forced).");
      }
    }
    if (e.affectsConfiguration("vitte.lint") || e.affectsConfiguration("vitte.features.lint") || e.affectsConfiguration("vitte.server.offline") || e.affectsConfiguration("vitte.server.offlinePermanent")) {
      for (const doc of vscode.workspace.textDocuments) updateEditorLint(doc);
    }
    if (e.affectsConfiguration("vitte.syntax.liveDiagnostics") || e.affectsConfiguration("vitte.lang")) {
      for (const doc of vscode.workspace.textDocuments) scheduleLiveSyntaxDiagnostics(doc, "config");
    }
    if (e.affectsConfiguration("vitte.suggestions.localEngine")) {
      localInlineEngine?.dispose();
      localInlineEngine = new LocalInlineEngine(readLocalInlineEngineOptions());
      void localInlineEngine.initialize(context);
      for (const doc of vscode.workspace.textDocuments) {
        scheduleLocalInlineEngineUpdate(doc);
      }
    }
    if (e.affectsConfiguration("vitte.suggestions.aiPipeline")) {
      aiInlinePipeline?.dispose();
      aiInlinePipeline = new AiInlinePipeline(readAiInlinePipelineOptions());
      void aiInlinePipeline.initialize();
      for (const doc of vscode.workspace.textDocuments) {
        if (isVitteDocument(doc)) void aiInlinePipeline.upsert(doc.uri.toString(), doc.getText());
      }
      updateSuggestionEngineModeSuffix();
    }
    if (e.affectsConfiguration("vitte.suggestions.ghostTextTop1Stable") || e.affectsConfiguration("vitte.suggestions.granularity")) {
      updateSuggestionEngineModeSuffix();
    }
  }));

  // Views are registered in best-effort mode so missing contributed view IDs
  // do not interrupt extension activation.
  // Register diagnostics/package views in best-effort mode.
  // View registration may be skipped when the contribution is absent, but command
  // registration remains available for tests and command palette usage.
  safeRegisterView("vitteDiagnostics", () => registerDiagnosticsView(context));
  // safeRegisterView("vitteModules", () => registerModuleExplorerView(context));
  // safeRegisterView("vitteMetrics", () => registerMetricsView(context, () => client, getStreamingCompletionStats));
  safeRegisterView("vittePackageProblems", () => registerPackageProblemsView(context));
  safeRegisterView("vitteModuleGraph", () => registerModuleGraphView(context));
  safeRegisterView("vitteTopSyntaxErrors", () => registerTopSyntaxErrorsView(context));
  safeRegisterView("vitteCommandCenter", () => registerCommandCenterView(context, () => client));
  // safeRegisterView("vitteOffline", () => registerOfflineView(
  //   context,
  //   () => offlineReason,
  //   () => output,
  //   () => formatOfflineSince(),
  //   () => {
  //     const summary = summarizeWorkspaceDiagnostics();
  //     const total = summary.errors + summary.warnings + summary.info + summary.hints;
  //     if (total === 0) return "No local diagnostics";
  //     return `${summary.errors} errors, ${summary.warnings} warnings`;
  //   }
  // ));
  context.subscriptions.push(vscode.languages.onDidChangeDiagnostics(() => refreshDiagnosticsStatus()));
  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(async (doc) => {
    if (!shouldFormatOnSave(doc)) return;
    if (formatOnSaveInFlight.has(doc.uri.toString())) return;
    const editor = vscode.window.visibleTextEditors.find((e) => e.document.uri.toString() === doc.uri.toString());
    if (!editor) return;
    formatOnSaveInFlight.add(doc.uri.toString());
    try {
      await vscode.window.showTextDocument(editor.document, { preview: false, preserveFocus: true });
      await vscode.commands.executeCommand("editor.action.formatDocument");
      if (editor.document.isDirty) {
        await editor.document.save();
      }
    } catch (err) {
      output.appendLine(`[format.onSave] ${String(err)}`);
    } finally {
      formatOnSaveInFlight.delete(doc.uri.toString());
    }
  }));
  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((doc) => {
    scheduleLiveSyntaxDiagnostics(doc, "save");
    scheduleLocalInlineEngineUpdate(doc);
  }));

  if (isOfflineEnabled()) {
    setOfflineStatus("Offline mode is enabled (vitte.server.offline).");
  }
  if (isOfflinePermanent()) {
    setOfflineStatus("Offline permanent (user-forced).");
  }

  startHealthChecks(context);

  if (process.env.VSCODE_TESTING === "1") {
    const api: ExtensionApi = {
      apiVersion: "1.0.0",
      extensionVersion: getExtensionVersion(context.extension),
      capabilities: {
        status: true,
        restart: true,
        runAction: true,
        serverResolutionTestHook: true,
      },
      getStatusText: () => statusItem?.text ?? "",
      getStatusTooltip: () => {
        const tip = statusItem?.tooltip;
        if (typeof tip === "string") return tip;
        if (tip instanceof vscode.MarkdownString) {
          return tip.value ?? "";
        }
        return "";
      },
      getClientState: () => client?.state,
      runAction: async (action: string) => {
        await runBuiltinAction(action);
      },
      restart: async () => {
        await requestClientRestart(context, "test-api");
      },
      resolveServerModuleForTest: (ctx) => resolveServerModule(ctx as vscode.ExtensionContext),
    };
    return api;
  }

  return undefined;
}

export async function deactivate(): Promise<void> {
  extensionShuttingDown = true;
  try { await client?.stop(); } catch { /* noop */ }
  client = undefined;
  cancelOfflineRetry();
  if (configRestartTimer) {
    clearTimeout(configRestartTimer);
    configRestartTimer = undefined;
  }
  if (completionIdlePrefetchTimer) {
    clearTimeout(completionIdlePrefetchTimer);
    completionIdlePrefetchTimer = undefined;
  }
  for (const timer of localInlineReindexTimers.values()) {
    try { clearTimeout(timer); } catch { /* noop */ }
  }
  localInlineReindexTimers.clear();
  if (localInlineEngine) {
    try { await localInlineEngine.persistNow(); } catch { /* noop */ }
    localInlineEngine.dispose();
    localInlineEngine = undefined;
  }
  aiInlinePipeline?.dispose();
  aiInlinePipeline = undefined;
  for (const timer of completionAstRefreshTimers.values()) {
    try { clearTimeout(timer); } catch { /* noop */ }
  }
  completionAstRefreshTimers.clear();
  for (const timer of syntaxLintTimers.values()) {
    try { clearTimeout(timer); } catch { /* noop */ }
  }
  syntaxLintTimers.clear();
  for (const proc of syntaxLintProcByDoc.values()) {
    try { proc.kill(); } catch { /* noop */ }
  }
  syntaxLintProcByDoc.clear();
  for (const timer of vitteExplainHelpCachePersistTimers.values()) {
    try { clearTimeout(timer); } catch { /* noop */ }
  }
  vitteExplainHelpCachePersistTimers.clear();
  completionLoadNextPageKey = undefined;
  completionLoadNextPageIssuedAt = 0;
  if (suggestionProfilerRenderTimer) {
    clearInterval(suggestionProfilerRenderTimer);
    suggestionProfilerRenderTimer = undefined;
  }
  try { suggestionProfilerPanel?.dispose(); } catch { /* noop */ }
  suggestionProfilerPanel = undefined;
  if (healthCheckTimer) {
    clearInterval(healthCheckTimer);
    healthCheckTimer = undefined;
  }
  healthFailures = 0;
  healthRestartInFlight = false;
  restartInFlight = undefined;
  restartQueued = false;
  restartQueueReason = "";
  reliabilityAttempts = 0;
  reliabilityNextDelayMs = 30000;
  recentStops.length = 0;
  offlineBannerShown = false;
  for (const watcher of fileWatchers) {
    try { watcher.dispose(); } catch { /* noop */ }
  }
  fileWatchers = [];
}

/* --------------------------------- LSP ----------------------------------- */

function resolveServerModule(context: vscode.ExtensionContext): string {
  // Permet d’overrider via settings: vitte.serverPath
  const cfgPath = vscode.workspace.getConfiguration("vitte").get<string>("serverPath");
  if (cfgPath) {
    if (fs.existsSync(cfgPath)) {
      logServerResolution(`Using custom server: ${cfgPath}`);
      return cfgPath;
    }
    logServerResolution(`Custom server path not found: ${cfgPath}`);
  }
  const nested = context.asAbsolutePath(path.join("server", "out", "src", "server.js"));
  if (fs.existsSync(nested)) {
    logServerResolution(`Using packaged server (server/out/src): ${nested}`);
    return nested;
  }
  const legacyNested = context.asAbsolutePath(path.join("server", "out", "server.js"));
  if (fs.existsSync(legacyNested)) {
    logServerResolution(`Using legacy server (server/out): ${legacyNested}`);
    return legacyNested;
  }
  const bundled = context.asAbsolutePath(path.join("out", "server.js"));
  if (fs.existsSync(bundled)) {
    logServerResolution(`Using embedded server: ${bundled}`);
    return bundled;
  }
  const message = "Module serveur Vitte introuvable (out/server.js ou server/out/server.js)";
  logServerResolution(message);
  throw new Error(message);
}

async function startClient(context: vscode.ExtensionContext | undefined): Promise<boolean> {
  if (extensionShuttingDown) return false;
  if (client) return true; // already running
  if (!context) return false;

  if (isOfflineEffective()) {
    const reason = isOfflinePermanent()
      ? "Offline permanent (user-forced)."
      : "Offline mode is enabled (vitte.server.offline).";
    setOfflineStatus(reason);
    return false;
  }

  let serverModule: string;
  try {
    serverModule = resolveServerModule(context);
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    setOfflineStatus(msg);
    void vscode.window.showWarningMessage(`Vitte: server offline — ${msg}`);
    return false;
  }
  const debugOptions = { execArgv: ["--nolazy", "--inspect=6009"] };
  const serverOptions: ServerOptions = {
    run:   { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc, options: debugOptions },
  };

  const documentSelector: DocumentSelector = LANGUAGES.flatMap((id) => ([
    { scheme: "file", language: id },
    { scheme: "untitled", language: id },
    { scheme: "vscode-notebook-cell", language: id }
  ]));
  const watchers = ensureFileWatchers(context);

  const clientOptions: LanguageClientOptions = {
    documentSelector,
    outputChannel: output,
    revealOutputChannelOn: RevealOutputChannelOn.Never,
    synchronize: {
      configurationSection: "vitte",
      fileEvents: watchers
    },
    middleware: {
      provideCompletionItem: async (
        document: vscode.TextDocument,
        position: vscode.Position,
        completionContext: vscode.CompletionContext,
        token: vscode.CancellationToken,
        next: (
          document: vscode.TextDocument,
          position: vscode.Position,
          context: vscode.CompletionContext,
          token: vscode.CancellationToken,
        ) => vscode.ProviderResult<vscode.CompletionItem[] | vscode.CompletionList>,
      ) => {
        const key = getCompletionRequestKey(document, position);
        const contextKey = getCompletionContextKey(document, position);
        const documentKey = getCompletionDocumentKey(document);
        const isPrefetch = completionPrefetchExpectedKey === key;
        if (completionLoadNextPageKey && (Date.now() - completionLoadNextPageIssuedAt) > 5000) {
          completionLoadNextPageKey = undefined;
          completionLoadNextPageIssuedAt = 0;
        }
        const forceLoadNextPage =
          completionLoadNextPageKey === key
          && (Date.now() - completionLoadNextPageIssuedAt) <= 5000;
        if (completionLoadNextPageKey === key) {
          completionLoadNextPageKey = undefined;
          completionLoadNextPageIssuedAt = 0;
        }
        let cancellationRecorded = false;
        const markRequestCancelled = (): void => {
          if (isPrefetch || cancellationRecorded) return;
          cancellationRecorded = true;
          completionFallbackCancelCount += 1;
          const trace = completionTraceActiveByKey.get(key);
          if (trace?.fallbackCause === "none") trace.fallbackCause = "cancel";
        };
        if (token.isCancellationRequested) {
          markRequestCancelled();
          return new vscode.CompletionList([], false);
        }

        if (!isPrefetch && completionSuggestionPendingAccepted) {
          completionSuggestionCanceledCount += 1;
          if (completionPendingRejectedItems.length > 0) {
            recordSuggestionRejections(completionPendingRejectedItems);
            completionPendingRejectedItems = [];
          }
          completionSuggestionPendingAccepted = false;
        }
        if (!isPrefetch && completionContext.triggerKind === vscode.CompletionTriggerKind.Invoke) {
          if (completionNextInvokeIsAutoRefresh) {
            completionNextInvokeIsAutoRefresh = false;
          } else {
            completionStreamingManualRefreshCount += 1;
          }
        }
        if (!isPrefetch) completionStreamingTotalCount += 1;
        const cfg = vscode.workspace.getConfiguration("vitte");
        const tuning: SuggestionTuning = {
          adaptiveRanking: cfg.get<boolean>("suggestions.adaptiveRanking", true),
          dynamicBudget: cfg.get<boolean>("suggestions.dynamicBudget", true),
          showExplainLabels: cfg.get<boolean>("suggestions.showExplainLabels", true),
          showMatchHighlights: cfg.get<boolean>("suggestions.showMatchHighlights", true),
          rankingVariant: cfg.get<"default" | "prefix_heavy" | "scope_heavy">("suggestions.rankingVariant", "default"),
          churnPenaltyEnabled: cfg.get<boolean>("suggestions.churnPenaltyEnabled", true),
          churnPenaltyWeight: cfg.get<number>("suggestions.churnPenaltyWeight", 1.0),
          typeBudgetEnabled: cfg.get<boolean>("suggestions.typeBudgetEnabled", true),
          functionBudgetRatio: cfg.get<number>("suggestions.functionBudgetRatio", 0.45),
          variableBudgetRatio: cfg.get<number>("suggestions.variableBudgetRatio", 0.35),
          snippetBudgetRatio: cfg.get<number>("suggestions.snippetBudgetRatio", 0.15),
          topN: cfg.get<number>("suggestions.topN", 50),
          minN: cfg.get<number>("suggestions.minN", 20),
          maxN: cfg.get<number>("suggestions.maxN", 80),
          slowMs: cfg.get<number>("suggestions.slowMs", 180),
          intentMode: cfg.get<"auto" | "code" | "test" | "refactor" | "docs" | "terminal">("suggestions.intentMode", "auto"),
          rankingDeadlineMs: Math.max(3, Math.min(120, cfg.get<number>("suggestions.rankingDeadlineMs", 25))),
        };
        const pagingEnabled = cfg.get<boolean>("suggestions.pagination", true);
        const pageSize = Math.max(5, Math.min(200, cfg.get<number>("suggestions.pageSize", 20)));
        const maxPages = Math.max(1, Math.min(10, cfg.get<number>("suggestions.maxPages", 5)));
        const pageResetMs = Math.max(500, Math.min(30000, cfg.get<number>("suggestions.pageResetMs", 5000)));
        const streamingEnabled = cfg.get<boolean>("suggestions.streaming", true);
        const firstPaintMs = cfg.get<number>("suggestions.streamingFirstPaintMs", 75);
        const localBatchSize = cfg.get<number>("suggestions.streamingLocalBatchSize", 40);
        const cachePrefixTtlMs = cfg.get<number>("suggestions.cachePrefixTtlMs", 1500);
        const cacheContextTtlMs = cfg.get<number>("suggestions.cacheContextTtlMs", 4000);
        const cacheDocumentTtlMs = cfg.get<number>("suggestions.cacheDocumentTtlMs", 12000);
        const cacheMaxEntries = Math.max(32, Math.min(5000, cfg.get<number>("suggestions.cacheMaxEntries", 1200)));
        const cacheSoftMemoryMb = Math.max(128, Math.min(8192, cfg.get<number>("suggestions.cacheSoftMemoryMb", 1536)));
        const shadowRankingEnabled = cfg.get<boolean>("suggestions.shadowRankingEnabled", false);
        const shadowRankingVariant = cfg.get<"default" | "prefix_heavy" | "scope_heavy">("suggestions.shadowRankingVariant", "scope_heavy");
        const autoRefresh = cfg.get<boolean>("suggestions.streamingAutoRefresh", true);
        const autoRefreshDebounceMinMs = Math.max(40, Math.min(500, cfg.get<number>("suggestions.autoRefreshDebounceMinMs", 80)));
        const autoRefreshDebounceMaxMs = Math.max(autoRefreshDebounceMinMs, Math.min(1000, cfg.get<number>("suggestions.autoRefreshDebounceMaxMs", 260)));
        const lspHardTimeoutMs = Math.max(50, Math.min(1000, cfg.get<number>("suggestions.lspHardTimeoutMs", 120)));
        const lintGuard = cfg.get<boolean>("suggestions.lintGuard", true);
        const diagnosticContextFilterEnabled = cfg.get<boolean>("suggestions.diagnosticContextFilterEnabled", true);
        const diagnosticContextMinTokenLen = Math.max(2, Math.min(12, cfg.get<number>("suggestions.diagnosticContextMinTokenLen", 4)));
        const securityGuardEnabled = cfg.get<boolean>("suggestions.securityGuardEnabled", true);
        const securityBlockedPatterns = cfg.get<string[]>("suggestions.securityBlockedPatterns", [
          "eval\\s*\\(",
          "exec\\s*\\(",
          "rm\\s+-rf",
          "curl\\s+.+\\|\\s*sh",
          "password\\s*=",
        ]) ?? [];
        const workspaceWhitelistPatterns = cfg.get<string[]>("suggestions.workspaceWhitelistPatterns", []) ?? [];
        const workspaceBlacklistPatterns = cfg.get<string[]>("suggestions.workspaceBlacklistPatterns", []) ?? [];
        const stickyTopEnabled = cfg.get<boolean>("suggestions.stickyTopEnabled", true);
        const stickyTopCount = Math.max(1, Math.min(32, cfg.get<number>("suggestions.stickyTopCount", 8)));
        const negativeCacheTimeoutThreshold = Math.max(1, Math.min(12, cfg.get<number>("suggestions.negativeCacheTimeoutThreshold", 3)));
        const negativeCacheHoldMs = Math.max(300, Math.min(15000, cfg.get<number>("suggestions.negativeCacheHoldMs", 2500)));
        const highLoadThreshold = Math.max(1, Math.min(16, cfg.get<number>("suggestions.highLoadThreshold", 6)));

        const toItems = (out: vscode.CompletionItem[] | vscode.CompletionList | null | undefined): vscode.CompletionItem[] => {
          if (!out) return [];
          return Array.isArray(out) ? out : out.items;
        };

        let requestedPage = 1;
        const isManualInvoke = !isPrefetch
          && completionContext.triggerKind === vscode.CompletionTriggerKind.Invoke
          && !completionNextInvokeIsAutoRefresh
          && !forceLoadNextPage;
        if (pagingEnabled) {
          const pageState = completionPagingState.get(key);
          const pageNow = Date.now();
          if (forceLoadNextPage && pageState) {
            requestedPage = Math.min(maxPages, pageState.page);
          } else if (isManualInvoke && pageState && (pageNow - pageState.ts) <= pageResetMs) {
            requestedPage = Math.min(maxPages, pageState.page + 1);
          } else if (isManualInvoke) {
            requestedPage = 1;
          } else {
            requestedPage = pageState?.page ?? 1;
          }
          completionPagingState.set(key, { page: requestedPage, ts: pageNow });
        }
        const budgetOverride = pagingEnabled ? requestedPage * pageSize : undefined;

        const finalize = (items: vscode.CompletionItem[], isIncomplete: boolean): vscode.CompletionList => {
          const guarded = items.filter((item) => (
            shouldKeepSuggestionWithLintGuard(item, document, position, lintGuard)
            && shouldKeepSuggestionWithDiagnosticContext(item, document, position, diagnosticContextFilterEnabled, diagnosticContextMinTokenLen)
            && shouldKeepSuggestionWithSecurityGuard(item, securityGuardEnabled, securityBlockedPatterns)
            && shouldKeepSuggestionWithPatternLists(item, workspaceWhitelistPatterns, workspaceBlacklistPatterns)
          ));
          const stickyLabels = stickyTopEnabled
            ? new Set((completionStickyAcceptedByDoc.get(document.uri.toString()) ?? []).slice(0, stickyTopCount))
            : undefined;
          const ranked = rankAndTrimCompletionItems(
            guarded,
            document,
            position,
            tuning,
            completionLatencyWindowMs,
            budgetOverride,
            stickyLabels,
          );
          if (!isPrefetch && shadowRankingEnabled) {
            const shadowTuning: SuggestionTuning = {
              ...tuning,
              showExplainLabels: false,
              showMatchHighlights: false,
              rankingVariant: shadowRankingVariant,
            };
            const shadow = rankAndTrimCompletionItems(
              guarded,
              document,
              position,
              shadowTuning,
              completionLatencyWindowMs,
              budgetOverride,
              stickyLabels,
            );
            const cmp = listAgreementAndDrift(ranked, shadow, 10);
            pushWindowSample(completionShadowAgreement, cmp.agreement);
            pushWindowSample(completionShadowDrift, cmp.drift);
          }
          const pagingIncomplete = pagingEnabled && guarded.length > ranked.length;
          const listItems = pagingIncomplete ? [...ranked, buildLoadMoreCompletionItem()] : ranked;
          if (!isPrefetch) updateTop1StableInlineCandidate(key, listItems);
          if (!isPrefetch) {
            updateSuggestionChurnForRequest(key, listItems.filter((it) => extractItemSource(it) !== "ui"));
          }
          if (!isPrefetch) {
            completionSuggestionShownCount += listItems.length;
            completionSuggestionPendingAccepted = listItems.length > 0;
            completionPendingRejectedItems = listItems.filter((it) => extractItemSource(it) !== "ui").slice(0, 50);
            const sourceCounts = new Map<string, number>();
            if (listItems.length >= 3 && !completionFirstUsableMarked.has(key)) {
              pushWindowSample(completionFirstUsableMs, Date.now() - requestStartedAt);
              completionFirstUsableMarked.add(key);
              const trace = completionTraceActiveByKey.get(key);
              if (trace) trace.firstUsableMs = Math.max(0, Date.now() - requestStartedAt);
            }
            for (const item of listItems) {
              const src = extractItemSource(item);
              incCounter(completionShownBySource, src);
              incCounter(sourceCounts, src);
            }
            const trace = completionTraceActiveByKey.get(key);
            if (trace) {
              trace.shownCount = listItems.filter((it) => extractItemSource(it) !== "ui").length;
              trace.sourceMix = [...sourceCounts.entries()]
                .filter(([src]) => src !== "ui")
                .sort((a, b) => b[1] - a[1])
                .map(([src, count]) => `${src}:${count}`)
                .join(" | ");
            }
          }
          return new vscode.CompletionList(listItems, isIncomplete || pagingIncomplete);
        };
        const now = Date.now();
        const requestStartedAt = now;
        const requestUri = document.uri.toString();
        const requestUriShort = vscode.workspace.asRelativePath(document.uri, false) || path.basename(document.uri.fsPath);
        if (!isPrefetch) {
          completionRequestStartedAt.set(key, now);
          completionRequestRefreshed.set(key, false);
          completionFirstUsableMarked.delete(key);
          completionTraceActiveByKey.set(key, {
            id: `${key}@${requestStartedAt}`,
            requestKey: key,
            languageId: document.languageId,
            uriShort: requestUriShort,
            startedAt: requestStartedAt,
            refreshCause: "none",
            fallbackCause: "none",
            shownCount: 0,
          });
        }
        if (!isPrefetch) completionCacheRequestCount += 1;
        const cachedItemsPrefix = getValidCacheItems(completionStreamingCachePrefix, key, cachePrefixTtlMs).map((item) => withSourceBadge(item, "cache", true));
        const cachedItemsContext = getValidCacheItems(completionStreamingCacheContext, contextKey, cacheContextTtlMs).map((item) => withSourceBadge(item, "cache", true));
        const cachedItemsDocument = getValidCacheItems(completionStreamingCacheDocument, documentKey, cacheDocumentTtlMs).map((item) => withSourceBadge(item, "cache", true));
        if (!isPrefetch && (cachedItemsPrefix.length > 0 || cachedItemsContext.length > 0 || cachedItemsDocument.length > 0)) {
          completionCacheHitCount += 1;
        }
        if (!isPrefetch && cachedItemsPrefix.length > 0 && completionPrefetchRequestCount > 0) {
          completionPrefetchHitCount += 1;
        }
        const load = os.loadavg()[0] ?? 0;
        const highLoad = load >= highLoadThreshold;
        const localItems = highLoad
          ? buildPrefixOnlyLocalBatch(document, position, Math.max(8, Math.min(localBatchSize, 20)))
          : buildLocalCompletionBatch(document, position, localBatchSize);
        const negativeCacheKey = `${document.languageId}`;
        const neg = completionLspNegativeCache.get(negativeCacheKey);
        const negativeCacheActive = !!neg && neg.until > Date.now();
        const seedMap = new Map<string, vscode.CompletionItem>();
        for (const item of [...cachedItemsPrefix, ...cachedItemsContext, ...cachedItemsDocument, ...localItems]) {
          seedMap.set(seedDedupKey(item), item);
        }
        const seeded = [...seedMap.values()];

        const requestPromise = (async () => {
          if (token.isCancellationRequested) {
            markRequestCancelled();
            return { out: null, items: [] as vscode.CompletionItem[] };
          }
          if (negativeCacheActive) {
            if (!isPrefetch) completionFallbackNegativeCacheCount += 1;
            if (!isPrefetch) {
              const trace = completionTraceActiveByKey.get(key);
              if (trace) trace.fallbackCause = "negative_cache";
            }
            return { out: null, items: [] as vscode.CompletionItem[] };
          }
          const t0 = Date.now();
          const out = await Promise.race([
            Promise.resolve(next(document, position, completionContext, token)),
            new Promise<null>((resolve) => setTimeout(() => resolve(null), lspHardTimeoutMs)),
          ]);
          const dt = Date.now() - t0;
          completionLatencyWindowMs.push(dt);
          if (completionLatencyWindowMs.length > 128) completionLatencyWindowMs.shift();
          if (!out && !isPrefetch) {
            completionStreamingTimeoutCount += 1;
            completionFallbackTimeoutCount += 1;
            const trace = completionTraceActiveByKey.get(key);
            if (trace) trace.fallbackCause = "timeout";
            const prev = completionLspNegativeCache.get(negativeCacheKey);
            const timeouts = (prev?.timeouts ?? 0) + 1;
            const until = timeouts >= negativeCacheTimeoutThreshold ? Date.now() + negativeCacheHoldMs : 0;
            completionLspNegativeCache.set(negativeCacheKey, { timeouts, until });
          } else if (!isPrefetch) {
            completionLspNegativeCache.delete(negativeCacheKey);
          }
          const items = toItems(out).map((item) => withSourceBadge(item, "lsp"));
          const ts = Date.now();
          touchMapEntry(completionStreamingCachePrefix, key, { items, ts });
          touchMapEntry(completionStreamingCacheContext, contextKey, { items, ts });
          touchMapEntry(completionStreamingCacheDocument, documentKey, { items, ts });
          trimCompletionCaches(cacheMaxEntries, cacheSoftMemoryMb);
          return { out, items };
        })();

        if (!streamingEnabled) {
          const { out, items } = await requestPromise;
          const result = finalize(items.length > 0 ? items : seeded, !out || Array.isArray(out) ? false : Boolean(out.isIncomplete));
          if (!isPrefetch) completeCompletionRequestMetrics(key, Date.now(), requestStartedAt);
          return result;
        }

        const inFlight = completionStreamingInFlight.get(key);
        if (!inFlight) {
          completionStreamingInFlight.set(key, requestPromise.then(async ({ out, items }) => {
            if (!isPrefetch) pushWindowSample(completionStreamingEnrichMs, Date.now() - now);
            if (!isPrefetch) pushWindowSample(completionStableListMs, Date.now() - now);
            if (!isPrefetch) {
              const trace = completionTraceActiveByKey.get(key);
              if (trace) {
                trace.enrichMs = Math.max(0, Date.now() - requestStartedAt);
                trace.stableMs = Math.max(0, Date.now() - requestStartedAt);
              }
            }
            const isIncomplete = !out || Array.isArray(out) ? false : out.isIncomplete;
            if (isPrefetch || !autoRefresh || items.length === 0) return;
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.uri.toString() !== requestUri) return;
            if (editor.selection.active.line !== position.line) return;
            if (Math.abs(editor.selection.active.character - position.character) > 2) return;
            if (completionStreamingRefreshGuard) return;
            completionStreamingRefreshGuard = true;
            try {
              if (isIncomplete || items.length > seeded.length) {
                completionStreamingRefreshCount += 1;
                if (isIncomplete) {
                  completionStreamingIncompleteRefreshCount += 1;
                  const trace = completionTraceActiveByKey.get(key);
                  if (trace) trace.refreshCause = "incomplete";
                } else {
                  completionStreamingRicherRefreshCount += 1;
                  const trace = completionTraceActiveByKey.get(key);
                  if (trace) trace.refreshCause = "richer";
                }
                completionRequestRefreshed.set(key, true);
                const docKey = document.uri.toString();
                const editBefore = typingSpeedByDoc.get(docKey)?.lastEditAt ?? 0;
                const debounceMs = getAdaptiveRefreshDebounceMs(docKey, autoRefreshDebounceMinMs, autoRefreshDebounceMaxMs);
                await new Promise((resolve) => setTimeout(resolve, debounceMs));
                const editAfter = typingSpeedByDoc.get(docKey)?.lastEditAt ?? 0;
                if (editAfter > editBefore) return;
                completionNextInvokeIsAutoRefresh = true;
                await vscode.commands.executeCommand("editor.action.triggerSuggest");
              }
            } finally {
              setTimeout(() => { completionStreamingRefreshGuard = false; }, 120);
            }
          }).finally(() => {
            if (!isPrefetch) completeCompletionRequestMetrics(key, Date.now(), requestStartedAt);
            completionStreamingInFlight.delete(key);
          }));
        } else if (!isPrefetch) {
          void inFlight.finally(() => {
            completeCompletionRequestMetrics(key, Date.now(), requestStartedAt);
          });
        }

        const raced = await Promise.race([
          requestPromise.then((v) => ({ type: "full" as const, ...v })),
          new Promise<{ type: "cancel" }>((resolve) => {
            const disposable = token.onCancellationRequested(() => {
              disposable.dispose();
              resolve({ type: "cancel" });
            });
          }),
          new Promise<{ type: "timeout" }>((resolve) => setTimeout(() => resolve({ type: "timeout" }), firstPaintMs)),
        ]);

        if (raced.type === "cancel") {
          markRequestCancelled();
          return new vscode.CompletionList([], false);
        }
        if (raced.type === "full") {
          if (token.isCancellationRequested) {
            markRequestCancelled();
            return new vscode.CompletionList([], false);
          }
          if (!isPrefetch) {
            pushWindowSample(completionStreamingFirstPaintMs, Date.now() - now);
            const trace = completionTraceActiveByKey.get(key);
            if (trace && trace.firstPaintMs === undefined) {
              trace.firstPaintMs = Math.max(0, Date.now() - requestStartedAt);
            }
            const opened = completionOpenedAtByDoc.get(document.uri.toString());
            if (opened) {
              completionColdStartCount += 1;
              pushWindowSample(completionColdStartMs, Date.now() - opened);
              completionOpenedAtByDoc.delete(document.uri.toString());
            }
            pushWindowSample(completionStableListMs, Date.now() - now);
          }
          return finalize(raced.items, !raced.out || Array.isArray(raced.out) ? false : Boolean(raced.out.isIncomplete));
        }
        if (!isPrefetch) {
          completionStreamingTimeoutCount += 1;
          completionFallbackTimeoutCount += 1;
          const trace = completionTraceActiveByKey.get(key);
          if (trace) {
            trace.firstPaintMs = Math.max(0, Date.now() - requestStartedAt);
            trace.fallbackCause = "timeout";
          }
          pushWindowSample(completionStreamingFirstPaintMs, Date.now() - now);
        }
        return finalize(seeded, true);
      },
      resolveCompletionItem: async (
        item: vscode.CompletionItem,
        token: vscode.CancellationToken,
        next: (item: vscode.CompletionItem, token: vscode.CancellationToken) => vscode.ProviderResult<vscode.CompletionItem>,
      ) => {
        recordSuggestionUsage(item);
        const src = extractItemSource(item);
        if (src !== "ui") {
          completionSuggestionAcceptedCount += 1;
          incCounter(completionAcceptedBySource, src);
          const stickyTopCount = Math.max(1, Math.min(32, vscode.workspace.getConfiguration("vitte").get<number>("suggestions.stickyTopCount", 8)));
          const docUri = vscode.window.activeTextEditor?.document.uri.toString();
          if (docUri) {
            recordStickyAcceptedLabel(docUri, item, stickyTopCount * 4);
          }
        }
        completionPendingRejectedItems = [];
        completionSuggestionPendingAccepted = false;
        return Promise.resolve(next(item, token));
      },
      provideDocumentFormattingEdits: async (
        doc: vscode.TextDocument,
        options: vscode.FormattingOptions,
        token: vscode.CancellationToken,
        next: ProvideDocumentFormattingEditsSignature
      ) => {
        try { return await next(doc, options, token); } catch {
          await vscode.commands.executeCommand("editor.action.formatDocument");
          return [];
        }
      },
    },
    initializationOptions: {
      // Extension → Serveur: options d’init (libre)
    },
  };

  client = new LanguageClient("vitte-lsp", "Vitte Language Server", serverOptions, clientOptions);

  client.onTelemetry((e: unknown) => {
    output.appendLine(`[telemetry] ${JSON.stringify(e)}`);
    obsLog("lsp.telemetry", "info", { payloadType: typeof e });
  });

  wireClientState(client);

  await client.start();
  return true;
}

async function restartClient(context: vscode.ExtensionContext | undefined): Promise<boolean> {
  if (client) {
    const previousClient = client;
    setStatusBase("$(sync)", "Vitte LSP: restarting…");
    // The restart command can run immediately after activation, while the
    // first client is still Starting. LanguageClient.stop() rejects in that
    // state; finish that start first so we do not orphan a live server.
    if (previousClient.state === ClientState.Starting) {
      try { await previousClient.start(); } catch { /* noop */ }
    }
    if (previousClient.state === ClientState.Running) {
      try { await previousClient.stop(); } catch { /* noop */ }
    }
    // LanguageClient.stop() can resolve before the child process has emitted
    // its final Stopped state. Do not start a replacement over that window:
    // late shutdown messages can otherwise close the new IPC connection.
    const deadline = Date.now() + 5000;
    while (previousClient.state !== ClientState.Stopped && Date.now() < deadline) {
      await sleep(25);
    }
    client = undefined;
  }
  if (isOfflineEffective()) {
    const reason = isOfflinePermanent()
      ? "Offline permanent (user-forced)."
      : "Offline mode is enabled (vitte.server.offline).";
    setOfflineStatus(reason);
    return false;
  }
  return startClient(context);
}

async function requestClientRestart(
  context: vscode.ExtensionContext | undefined,
  reason: string
): Promise<boolean> {
  if (extensionShuttingDown) return false;
  if (restartInFlight) {
    restartQueued = true;
    restartQueueReason = reason;
    obsLog("restart.coalesced", "info", { reason });
    return restartInFlight;
  }
  restartInFlight = (async () => {
    let lastResult = false;
    let activeReason = reason;
    do {
      restartQueued = false;
      const startedAt = Date.now();
      obsLog("restart.begin", "info", { reason: activeReason });
      lastResult = await restartClient(context);
      obsLog("restart.end", lastResult ? "info" : "warn", {
        reason: activeReason,
        ok: lastResult,
        elapsedMs: Date.now() - startedAt,
      });
      activeReason = restartQueueReason || reason;
      restartQueueReason = "";
    } while (restartQueued);
    return lastResult;
  })().finally(() => {
    restartInFlight = undefined;
    restartQueued = false;
    restartQueueReason = "";
  });
  return restartInFlight;
}

function scheduleConfigRestart(context: vscode.ExtensionContext | undefined, reason: string): void {
  if (extensionShuttingDown) return;
  if (configRestartTimer) clearTimeout(configRestartTimer);
  configRestartTimer = setTimeout(() => {
    if (extensionShuttingDown) return;
    configRestartTimer = undefined;
    void requestClientRestart(context, reason);
  }, 450);
}

function shouldRestartForConfigChange(e: vscode.ConfigurationChangeEvent): boolean {
  return RESTART_RELEVANT_CONFIG_KEYS.some((key) => e.affectsConfiguration(key));
}

function startHealthChecks(context: vscode.ExtensionContext): void {
  if (extensionShuttingDown) return;
  if (healthCheckTimer) clearInterval(healthCheckTimer);
  let lastBudgetAlert = 0;
  healthCheckTimer = setInterval(() => {
    void (async () => {
      if (extensionShuttingDown) return;
      if (isOfflineEffective()) return;
      if (!client || client.state !== ClientState.Running) return;
      try {
        const requestId = nextRequestId("health-ping");
        await client.sendRequest("vitte/ping");
        obsLog("health.ping.ok", "info", undefined, requestId);
        healthFailures = 0;
        try {
          const metrics = await client.sendRequest<ServerMetricEntry[]>("vitte/metrics");
          const cfg = vscode.workspace.getConfiguration("vitte");
          const budgets = {
            completion: cfg.get<number>("semanticBudget.completionP95Ms", 900),
            hover: cfg.get<number>("semanticBudget.hoverP95Ms", 600),
            rename: cfg.get<number>("semanticBudget.renameP95Ms", 1200),
            references: cfg.get<number>("semanticBudget.referencesP95Ms", 1200),
          };
          const over: string[] = [];
          for (const key of Object.keys(budgets) as (keyof typeof budgets)[]) {
            const m = metrics.find((x) => x.name === key);
            const p95 = m?.p95Ms ?? m?.averageMs ?? 0;
            if (p95 > budgets[key]) over.push(`${key} ${p95.toFixed(1)}>${budgets[key]}`);
          }
          if (over.length > 0 && Date.now() - lastBudgetAlert > 120000) {
            lastBudgetAlert = Date.now();
            void vscode.window.showWarningMessage(`Vitte semantic budget exceeded: ${over.join(" | ")}`);
            obsLog("health.semanticBudget.exceeded", "warn", { over: over.join(" | ") });
          }
        } catch {
          // ignore budget telemetry errors
        }
      } catch (err) {
        healthFailures += 1;
        output.appendLine(`[health] ping failure #${healthFailures}: ${String(err)}`);
        obsLog("health.ping.failed", "error", { failures: healthFailures, message: String(err) });
        if (healthFailures < 2) return;
        if (healthRestartInFlight) return;
        const cfg = vscode.workspace.getConfiguration("vitte");
        const maxAttempts = Math.max(1, cfg.get<number>("reliability.maxRestartAttempts", 3));
        const baseRetry = Math.max(1000, cfg.get<number>("reliability.baseRetryMs", 30000));
        const maxRetry = Math.max(baseRetry, cfg.get<number>("reliability.maxRetryMs", 300000));
        const cooldownMs = Math.max(10000, cfg.get<number>("reliability.cooldownMs", 120000));
        if (reliabilityAttempts >= maxAttempts) {
          setStatusOverride(undefined, `Reliability guard open-circuit (${reliabilityAttempts}/${maxAttempts})`);
          setTimeout(() => {
            reliabilityAttempts = 0;
            reliabilityNextDelayMs = baseRetry;
          }, cooldownMs);
          return;
        }
        healthRestartInFlight = true;
        try {
          const jitter = Math.floor(Math.random() * Math.max(1, Math.floor(reliabilityNextDelayMs * 0.25)));
          await sleep(reliabilityNextDelayMs + jitter);
          obsLog("health.restart.attempt", "warn", { delayMs: reliabilityNextDelayMs + jitter, attempt: reliabilityAttempts + 1 });
          const ok = await requestClientRestart(context, "health-check");
          if (!ok) {
            setOfflineStatus("Health check restart failed.");
            obsLog("health.restart.failed", "error");
          } else {
            healthFailures = 0;
            reliabilityAttempts = 0;
            reliabilityNextDelayMs = baseRetry;
            obsLog("health.restart.ok", "info");
          }
          reliabilityAttempts += 1;
          reliabilityNextDelayMs = Math.min(maxRetry, Math.max(baseRetry, reliabilityNextDelayMs * 2));
        } finally {
          healthRestartInFlight = false;
        }
      }
    })();
  }, 30000);
}

function wireClientState(c: LanguageClient): void {
  c.onDidChangeState((e: { oldState: ClientState; newState: ClientState }) => {
    obsLog("lsp.state.changed", "info", {
      oldState: ClientState[e.oldState],
      newState: ClientState[e.newState],
    });
    if (e.newState === ClientState.Starting) {
      setStatusBase("$(gear)", "Vitte LSP: starting");
      void setServerOnlineContext(false);
    } else if (e.newState === ClientState.Running) {
      offlineReason = undefined;
      offlineSince = undefined;
      offlineBannerShown = false;
      cancelOfflineRetry();
      setStatusBase("$(check)", "Vitte LSP: running");
      void setServerOnlineContext(true);
    } else if (e.newState === ClientState.Stopped) {
      setStatusBase("$(debug-stop)", "Vitte LSP: stopped");
      void setServerOnlineContext(false);
      const now = Date.now();
      recentStops.push(now);
      while (recentStops.length) {
        const first = recentStops[0];
        if (first === undefined) break;
        if ((now - first) <= 120000) break;
        recentStops.shift();
      }
      if (!isOfflineEnabled() && recentStops.length >= 3) {
        setOfflineStatus("Server stopped repeatedly (3x in 2 minutes).");
      }
    }
  });

  c.onNotification("vitte/status", (msg: { text?: string; tooltip?: string }) => {
    const text = typeof msg?.text === "string" ? msg.text : undefined;
    const tooltip = typeof msg?.tooltip === "string" ? msg.tooltip : undefined;
    if (text !== undefined || tooltip !== undefined) {
      setStatusOverride(text, tooltip);
    }
  });

  c.onNotification("vitte/log", (msg: unknown) => {
    output.appendLine(typeof msg === "string" ? msg : JSON.stringify(msg));
    if (typeof msg === "string") {
      obsLog("lsp.log", "info", { message: msg.slice(0, 240) });
    } else if (msg && typeof msg === "object") {
      const rec = msg as Record<string, unknown>;
      const requestId = typeof rec.requestId === "string" ? rec.requestId : undefined;
      const event = typeof rec.event === "string" ? rec.event : "lsp.log.object";
      obsLog(event, "info", { keys: Object.keys(rec).slice(0, 12).join(",") }, requestId);
    }
  });
}

/* ----------------------------- Actions utilitaires ------------------------ */

async function runBuiltinAction(action: string): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    void vscode.window.showWarningMessage("Open a Vitte/Vit document before running this action.");
    return;
  }
  const languageId = editor.document.languageId;
  if (!LANGUAGE_SET.has(languageId)) {
    void vscode.window.showWarningMessage("Les actions Vitte ne sont disponibles que pour les fichiers Vitte/Vit.");
    return;
  }
  switch (action) {
    case "format":
      await vscode.commands.executeCommand("editor.action.formatDocument");
      return;
    case "organizeImports":
      await vscode.commands.executeCommand("editor.action.organizeImports");
      return;
    case "fixAll":
      await vscode.commands.executeCommand("editor.action.codeAction", {
        kind: vscode.CodeActionKind.SourceFixAll.value,
        apply: "first"
      });
      return;
    default:
      void vscode.window.showWarningMessage(`Action inconnue: ${action}`);
      return;
  }
}

function showOfflineNoop(action: string): void {
  const reason = offlineReason ?? (isOfflinePermanent()
    ? "Offline permanent (user-forced)."
    : "Offline mode is enabled (vitte.server.offline).");
  const summary = summarizeWorkspaceDiagnostics();
  output.appendLine(
    `[offline-fallback:${action}] ${reason} | local diagnostics: ${summary.errors}e/${summary.warnings}w/${summary.info}i/${summary.hints}h`
  );
  obsLog("offline.noop", "warn", { action, reason });
  void vscode.window.showWarningMessage(
    `Vitte: ${action} unavailable while offline — ${reason}`,
    "Explain Offline",
    "Open Offline View",
    "Copy Offline Report",
  ).then(async (pick) => {
    if (pick === "Explain Offline") {
      await vscode.commands.executeCommand("vitte.offline.explain");
      return;
    }
    if (pick === "Open Offline View") {
      await vscode.commands.executeCommand("workbench.view.extension.vitte-sidebar");
      await vscode.commands.executeCommand("vitte.offline.refresh");
      return;
    }
    if (pick === "Copy Offline Report") {
      const report = await readOfflineReport();
      await vscode.env.clipboard.writeText(report);
      void vscode.window.showInformationMessage("Vitte: offline report copied to clipboard.");
    }
  });
}

function safeRegisterView(label: string, register: () => void): void {
  try {
    register();
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    output.appendLine(`[view.register.skip:${label}] ${message}`);
    obsLog("view.register.skipped", "warn", { view: label, message });
  }
}

function ensureTestingWorkspaceFolder(context: vscode.ExtensionContext): void {
  if ((vscode.workspace.workspaceFolders?.length ?? 0) > 0) return;
  const uri = vscode.Uri.file(context.extensionPath);
  try {
    const ok = vscode.workspace.updateWorkspaceFolders(0, 0, { uri, name: "VitteLangVsCode" });
    output.appendLine(`[test.workspace] injected=${String(ok)} uri=${uri.fsPath}`);
  } catch (err) {
    output.appendLine(`[test.workspace] inject failed: ${String(err)}`);
  }
}

async function ensureBaselineContributedCommands(context: vscode.ExtensionContext): Promise<void> {
  const required = readContributedVitteCommands(context);
  const existing = new Set(await vscode.commands.getCommands(true));
  for (const command of required) {
    if (existing.has(command)) continue;
    context.subscriptions.push(vscode.commands.registerCommand(command, () => undefined));
    output.appendLine(`[command.fallback] registered missing command: ${command}`);
  }
}

function readContributedVitteCommands(context: vscode.ExtensionContext): string[] {
  try {
    const raw = fs.readFileSync(context.asAbsolutePath("package.json"), "utf8");
    const pkg = JSON.parse(raw) as { contributes?: { commands?: { command?: unknown }[] } };
    return (pkg.contributes?.commands ?? [])
      .map((entry) => entry.command)
      .filter((command): command is string => typeof command === "string" && command.startsWith("vitte."))
      .sort((a, b) => a.localeCompare(b));
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    output.appendLine(`[command.fallback] package scan failed: ${message}`);
    return [
      "vitte.diagnostics.refresh",
      "vitte.diagnostics.open",
      "vitte.diagnostics.copy",
      "vitte.diagnostics.explain",
      "vitte.diagnostics.copyExplainCommand",
      "vitte.diagnostics.openDoc",
      "vitte.packageProblems.refresh",
      "vitte.packageProblems.open",
      "vitte.topSyntaxErrors.refresh",
      "vitte.topSyntaxErrors.setCodeFilter",
      "vitte.topSyntaxErrors.clearCodeFilter",
    ];
  }
}

function safeWorkspaceSubPath(...segments: string[]): string | undefined {
  const root = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!root) return undefined;
  const target = path.resolve(root, ...segments);
  const rel = path.relative(root, target);
  if (rel.startsWith("..") || path.isAbsolute(rel)) return undefined;
  return target;
}

function diagnosticsExportDir(): string | undefined {
  return safeWorkspaceSubPath(".vitte-cache", "diagnostics");
}

async function readOfflineReport(): Promise<string> {
  const cfg = vscode.workspace.getConfiguration("vitte");
  const report: string[] = [];
  report.push(`# Vitte Offline Report`);
  report.push(`- offline: ${isOfflineEffective()}`);
  report.push(`- offlinePermanent: ${isOfflinePermanent()}`);
  report.push(`- offlineSince: ${offlineSince ? new Date(offlineSince).toISOString() : "unknown"}`);
  report.push(`- offlineReason: ${offlineReason ?? "unknown"}`);
  report.push(`- autoRetry: ${cfg.get<boolean>("server.autoRetry", true)}`);
  report.push(`- autoRetryBaseMs: ${cfg.get<number>("server.autoRetryBaseMs", 60000)}`);
  report.push(`- autoRetryMaxMs: ${cfg.get<number>("server.autoRetryMaxMs", 300000)}`);
  report.push(`- workspaceFolders: ${(vscode.workspace.workspaceFolders ?? []).length}`);
  report.push(`- openEditors: ${vscode.workspace.textDocuments.length}`);
  report.push(`- diagnostics (local): ${summarizeWorkspaceDiagnostics().errors} errors, ${summarizeWorkspaceDiagnostics().warnings} warnings`);
  report.push(`- offlineLog: ${getOfflineLogPathSafe()}`);
  const tail = await readOfflineLogTail(30);
  if (tail) {
    report.push(`\n## offline.log (last 30 lines)\n${tail}`);
  }
  return report.join("\n");
}

function getOfflineLogPathSafe(): string {
  try {
    const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    const base = folder ?? os.tmpdir();
    return path.join(base, ".vitte", "offline.log");
  } catch {
    return "unknown";
  }
}

async function readOfflineLogTail(lines: number): Promise<string> {
  try {
    const filePath = getOfflineLogPathSafe();
    const content = await fs.promises.readFile(filePath, "utf8");
    const rows = content.trim().split(/\r?\n/);
    return rows.slice(-lines).join("\n");
  } catch {
    return "";
  }
}

function isVitteDocument(doc: vscode.TextDocument): boolean {
  return LANGUAGE_SET.has(doc.languageId as typeof LANGUAGES[number]);
}

function shouldRunLiveSyntaxDiagnostics(doc: vscode.TextDocument): boolean {
  if (!syntaxLintCollection) return false;
  if (!isVitteDocument(doc)) return false;
  if (doc.uri.scheme !== "file" || doc.isDirty) return false;
  const cfg = vscode.workspace.getConfiguration("vitte");
  return cfg.get<boolean>("syntax.liveDiagnostics", true);
}

function isLiveSyntaxTarget(doc: vscode.TextDocument): boolean {
  const key = doc.uri.toString();
  const active = vscode.window.activeTextEditor?.document.uri.toString() === key;
  if (active) return true;
  return vscode.window.visibleTextEditors.some((e) => e.document.uri.toString() === key);
}

function normalizeDiagSeverityToken(severity: string | undefined): string {
  return (severity ?? "").trim().toLowerCase();
}

function mapDiagSeverity(severity: string | undefined): vscode.DiagnosticSeverity {
  const token = normalizeDiagSeverityToken(severity);
  switch (token) {
    case "error":
    case "err":
    case "fatal":
    case "bug":
    case "ice":
      return vscode.DiagnosticSeverity.Error;
    case "warning":
    case "warn":
      return vscode.DiagnosticSeverity.Warning;
    case "hint":
      return vscode.DiagnosticSeverity.Hint;
    case "information":
    case "info":
    case "note":
    case "help":
      return vscode.DiagnosticSeverity.Information;
    default:
      return vscode.DiagnosticSeverity.Error;
  }
}

function normalizeDiagCode(raw: string | number | undefined): string {
  if (typeof raw === "number") return String(raw);
  if (typeof raw !== "string") return "";
  return raw.trim().toUpperCase();
}

function diagnosticCodeText(raw: vscode.Diagnostic["code"]): string {
  if (typeof raw === "string" || typeof raw === "number") return String(raw).trim();
  if (raw && typeof raw === "object" && "value" in raw) {
    const value = (raw as { value?: unknown }).value;
    if (typeof value === "string" || typeof value === "number") return String(value).trim();
  }
  return "";
}

function strictDiagnosticDedupKey(
  range: vscode.Range,
  source: string,
  code: string,
): string {
  const span = `${range.start.line}:${range.start.character}-${range.end.line}:${range.end.character}`;
  const src = source.trim().toLowerCase() || "unknown";
  const c = normalizeDiagCode(code) || "NO_CODE";
  return `${span}|${c}|${src}`;
}

function dedupeDiagnosticsBySpanCodeSource(diagnostics: vscode.Diagnostic[]): vscode.Diagnostic[] {
  const seen = new Set<string>();
  const deduped: vscode.Diagnostic[] = [];
  for (const d of diagnostics) {
    const key = strictDiagnosticDedupKey(d.range, String(d.source ?? ""), diagnosticCodeText(d.code));
    if (seen.has(key)) continue;
    seen.add(key);
    deduped.push(d);
  }
  return deduped;
}

function prefixedDiagCode(prefix: string, code: string): string {
  const p = prefix.trim().toUpperCase();
  const c = code.trim().toUpperCase();
  if (!p) return c;
  if (!c) return p;
  return `${p}:${c}`;
}

function explainableDiagCode(code: string): string | undefined {
  const text = code.trim().toUpperCase();
  if (!text) return undefined;
  const prefixed = /^([A-Z][A-Z0-9_-]*):(.*)$/.exec(text);
  const base = (prefixed?.[2] ?? text).trim().toUpperCase();
  if (/^E\d{4}$/.test(base) || /^VITTE-[A-Z]\d{4}$/.test(base)) return base;
  return undefined;
}

function diagnosticDocUri(code: string): vscode.Uri | undefined {
  const normalized = explainableDiagCode(code);
  if (normalized) {
    return vscode.Uri.parse(`https://docs.vitte.dev/diagnostics/${normalized}`);
  }
  return undefined;
}

type DiagnosticHelpSource = "auto" | "vitte" | "local";
type SyntaxFirstErrorMode = "line" | "file";

function vitteExplainHelpCachePath(cwd: string): string {
  return path.join(cwd, ".vitte-cache", "diagnostics", "help-cache.json");
}

function resolveVitteVersionSignature(resolved: { bin: string; cwd: string }): string {
  const existing = vitteVersionSignatureByBin.get(resolved.bin);
  if (existing) return existing;
  try {
    const out = cp.spawnSync(
      resolved.bin,
      ["--version"],
      { cwd: resolved.cwd, shell: false, encoding: "utf8", timeout: 350, maxBuffer: 64 * 1024 }
    );
    const text = `${out.stdout ?? ""}\n${out.stderr ?? ""}`.trim();
    const m = /(\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?)/.exec(text);
    if (m?.[1]) {
      const sig = `vitte-${m[1]}`;
      vitteVersionSignatureByBin.set(resolved.bin, sig);
      return sig;
    }
  } catch {
    // ignore and fallback
  }
  try {
    const stats = fs.statSync(resolved.bin);
    const sig = `bin:${resolved.bin}|mtime:${Math.trunc(stats.mtimeMs)}|size:${stats.size}`;
    vitteVersionSignatureByBin.set(resolved.bin, sig);
    return sig;
  } catch {
    const sig = `bin:${resolved.bin}|unknown`;
    vitteVersionSignatureByBin.set(resolved.bin, sig);
    return sig;
  }
}

function touchVitteExplainHelpCache(key: string, help: string, at: number, example?: string): void {
  if (vitteExplainHelpCache.has(key)) {
    vitteExplainHelpCache.delete(key);
  }
  const next: { help: string; example?: string; at: number } = { help, at };
  if (example) next.example = example;
  vitteExplainHelpCache.set(key, next);
}

function loadVitteExplainHelpCache(cachePath: string, versionSignature: string): void {
  const loadKey = `${cachePath}::${versionSignature}`;
  if (vitteExplainHelpCacheLoadedFiles.has(loadKey)) return;
  vitteExplainHelpCacheLoadedFiles.add(loadKey);
  try {
    const raw = fs.readFileSync(cachePath, "utf8");
    const parsed = JSON.parse(raw) as {
      schema?: number;
      versionSignature?: string;
      entries?: Record<string, { help?: string; example?: string; at?: number }>;
    };
    if (parsed.versionSignature && parsed.versionSignature !== versionSignature) {
      return;
    }
    const entries = parsed.entries ?? {};
    for (const [key, value] of Object.entries(entries)) {
      const help = typeof value?.help === "string" ? value.help.trim() : "";
      const example = typeof value?.example === "string" ? value.example.trim() : undefined;
      const at = typeof value?.at === "number" && Number.isFinite(value.at) ? value.at : Date.now();
      if (!help) continue;
      touchVitteExplainHelpCache(key, help, at, example);
    }
  } catch {
    // missing/invalid cache file => ignore
  }
}

function persistVitteExplainHelpCache(cachePath: string, versionSignature: string): void {
  try {
    fs.mkdirSync(path.dirname(cachePath), { recursive: true });
    const entries = [...vitteExplainHelpCache.entries()]
      .filter(([key]) => key.startsWith(`${versionSignature}|`))
      .sort((a, b) => (b[1].at ?? 0) - (a[1].at ?? 0))
      .slice(0, 800);
    const payload: Record<string, { help: string; example?: string; at: number }> = {};
    for (const [key, value] of entries) {
      const row: { help: string; example?: string; at: number } = { help: value.help, at: value.at };
      if (value.example) row.example = value.example;
      payload[key] = row;
    }
    fs.writeFileSync(
      cachePath,
      `${JSON.stringify({ schema: 1, versionSignature, entries: payload }, null, 2)}\n`,
      "utf8"
    );
  } catch {
    // ignore persistence errors
  }
}

function schedulePersistVitteExplainHelpCache(cachePath: string, versionSignature: string): void {
  const prev = vitteExplainHelpCachePersistTimers.get(cachePath);
  if (prev) clearTimeout(prev);
  const timer = setTimeout(() => {
    vitteExplainHelpCachePersistTimers.delete(cachePath);
    persistVitteExplainHelpCache(cachePath, versionSignature);
  }, 250);
  vitteExplainHelpCachePersistTimers.set(cachePath, timer);
}

function clearVitteExplainHelpCache(workspaceFolders: readonly vscode.WorkspaceFolder[] | undefined): { deletedFiles: number; failedFiles: number } {
  for (const timer of vitteExplainHelpCachePersistTimers.values()) {
    clearTimeout(timer);
  }
  vitteExplainHelpCachePersistTimers.clear();
  vitteExplainHelpCache.clear();
  vitteExplainHelpCacheLoadedFiles.clear();
  vitteVersionSignatureByBin.clear();

  let deletedFiles = 0;
  let failedFiles = 0;
  for (const folder of workspaceFolders ?? []) {
    const cachePath = vitteExplainHelpCachePath(folder.uri.fsPath);
    try {
      if (fs.existsSync(cachePath)) {
        fs.unlinkSync(cachePath);
        deletedFiles += 1;
      }
    } catch {
      failedFiles += 1;
    }
  }
  return { deletedFiles, failedFiles };
}

function diagnosticHelpObservabilitySnapshot(): DiagnosticHelpObservabilitySnapshot {
  const requests = diagnosticHelpRequests;
  const explainResolved = diagnosticHelpExplainResolved;
  const explainThrottled = diagnosticHelpExplainThrottled;
  const localFallbackResolved = diagnosticHelpLocalFallbackResolved;
  const localOnlyResolved = diagnosticHelpLocalOnlyResolved;
  const unresolved = diagnosticHelpUnresolved;
  const safeRate = (num: number): number => (requests > 0 ? Number((num / requests).toFixed(4)) : 0);
  return {
    requests,
    explainResolved,
    explainThrottled,
    localFallbackResolved,
    localOnlyResolved,
    unresolved,
    explainUsageRate: safeRate(explainResolved),
    explainThrottledRate: safeRate(explainThrottled),
    localFallbackRate: safeRate(localFallbackResolved),
    localOnlyRate: safeRate(localOnlyResolved),
  };
}

function shouldRunExplainNow(): boolean {
  const now = Date.now();
  while (explainCallTimestamps.length > 0 && (now - explainCallTimestamps[0]!) > EXPLAIN_THROTTLE_WINDOW_MS) {
    explainCallTimestamps.shift();
  }
  if (explainCallTimestamps.length >= EXPLAIN_THROTTLE_MAX_CALLS) return false;
  explainCallTimestamps.push(now);
  return true;
}

function explainHelpFromVitte(
  code: string,
  lang: string,
  resolved: { bin: string; cwd: string },
  explainTimeoutMs: number
): DiagnosticExplainPayload | undefined {
  if (!/^E\d{4}$/.test(code) && !/^VITTE-[A-Z]\d{4}$/.test(code)) return undefined;
  const versionSignature = resolveVitteVersionSignature(resolved);
  const cachePath = vitteExplainHelpCachePath(resolved.cwd);
  loadVitteExplainHelpCache(cachePath, versionSignature);
  const key = `${versionSignature}|${lang}|${code}`;
  const now = Date.now();
  const cached = vitteExplainHelpCache.get(key);
  if (cached && (now - cached.at) <= 6 * 60 * 60 * 1000) {
    const cachedPayload: DiagnosticExplainPayload = { help: cached.help, source: "external" };
    if (cached.example) cachedPayload.example = cached.example;
    return cachedPayload;
  }
  if (!shouldRunExplainNow()) {
    diagnosticHelpExplainThrottled += 1;
    return undefined;
  }
  try {
    const out = cp.spawnSync(
      resolved.bin,
      ["explain", code],
      {
        cwd: resolved.cwd,
        shell: false,
        encoding: "utf8",
        timeout: explainTimeoutMs,
        maxBuffer: 128 * 1024,
      }
    );
    const text = `${out.stdout ?? ""}\n${out.stderr ?? ""}`;
    const lines = text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
    const fix = lines.find((line) => /^Fix:\s*/i.test(line));
    const summary = lines.find((line) => /^Summary:\s*/i.test(line));
    const picked = (fix ?? summary ?? "").replace(/^(Fix|Summary):\s*/i, "").trim();
    const guidanceIndex = lines.findIndex(line => line === "Guidance");
    const guidance = guidanceIndex >= 0 ? lines.slice(guidanceIndex + 2).join(" ").trim() : "";
    if (!picked && !guidance) return undefined;
    const exampleIndex = lines.findIndex((line) => /^Example:\s*/i.test(line));
    let example: string | undefined;
    if (exampleIndex >= 0) {
      const current = (lines[exampleIndex] ?? "").replace(/^Example:\s*/i, "").trim();
      const extra: string[] = [];
      for (let i = exampleIndex + 1; i < lines.length && extra.length < 3; i++) {
        const line = lines[i];
        if (!line) break;
        if (/^(Fix|Summary|Code|Hint|Notes?):\s*/i.test(line)) break;
        extra.push(line);
      }
      const collected = [current, ...extra].filter(Boolean);
      if (collected.length > 0) {
        example = collected.join("\n");
      }
    }
    touchVitteExplainHelpCache(key, picked || guidance, now, example);
    schedulePersistVitteExplainHelpCache(cachePath, versionSignature);
    const payload: DiagnosticExplainPayload = { help: picked || guidance, source: "external" };
    if (example) payload.example = example;
    return payload;
  } catch {
    return undefined;
  }
}

function vitteDiagnosticHelpForCode(code: string): string | undefined {
  if (code === "E0001") return "expected identifier; use letters/digits/_ and avoid leading digits";
  if (code === "E0002") return "expected expression; try a literal, name, call, or block expression";
  if (code === "E0003") return "expected pattern; use identifier, constructor, or tuple/list pattern";
  if (code === "E0004") return "expected type; try i32/string/bool or a generic type";
  if (code === "E0005") return "unterminated block; close with `.end`";
  if (code === "E0006") return "attribute must be followed by a `proc` declaration";
  if (code === "E0007") return "unexpected top-level token; remove orphan token or close previous construct";
  if (code === "E1016") return "internal module import denied; import the public facade instead";
  if (code === "E1017") return "re-export conflict; replace glob imports with explicit symbol imports";
  if (code === "E1018") return "ambiguous import path; keep either file-form or directory-form module";
  if (/^VITTE-P\d{4}$/.test(code)) return "process diagnostic; check timeout/grace/profile/allowlist policy";
  return undefined;
}

function resolveDiagnosticHelp(
  code: string,
  lang: string,
  resolved: { bin: string; cwd: string },
  helpSource: DiagnosticHelpSource,
  explainTimeoutMs: number
): DiagnosticExplainPayload | undefined {
  diagnosticHelpRequests += 1;
  if (helpSource === "local") {
    const local = vitteDiagnosticHelpForCode(code);
    if (local) {
      diagnosticHelpLocalOnlyResolved += 1;
      return { help: local, source: "local" };
    }
    diagnosticHelpUnresolved += 1;
    return undefined;
  }
  if (helpSource === "vitte") {
    const remote = explainHelpFromVitte(code, lang, resolved, explainTimeoutMs);
    if (remote) {
      diagnosticHelpExplainResolved += 1;
      return remote;
    }
    diagnosticHelpUnresolved += 1;
    return undefined;
  }
  const remote = explainHelpFromVitte(code, lang, resolved, explainTimeoutMs);
  if (remote) {
    diagnosticHelpExplainResolved += 1;
    return remote;
  }
  const local = vitteDiagnosticHelpForCode(code);
  if (local) {
    diagnosticHelpLocalFallbackResolved += 1;
    return { help: local, source: "local" };
  }
  diagnosticHelpUnresolved += 1;
  return undefined;
}

function formatVitteDiagnosticMessage(base: string, code: string, explain?: DiagnosticExplainPayload): string {
  const help = explain?.help ?? vitteDiagnosticHelpForCode(code);
  if (!help) return base;
  const example = explain?.example?.trim();
  const lang = vscode.workspace.getConfiguration("vitte").get<string>("lang", "en").trim().toLowerCase();
  const isFr = lang.startsWith("fr");
  const helpLabel = isFr ? "aide" : "help";
  const sourceLabel = isFr ? "source-aide" : "help-source";
  const exampleLabel = isFr ? "exemple" : "example";
  const sourceTag = explain?.source === "external" ? `\n${sourceLabel}: external` : "";
  if (!example) return `${base}\n${helpLabel}: ${help}${sourceTag}`;
  return `${base}\n${helpLabel}: ${help}${sourceTag}\n${exampleLabel}:\n${example}`;
}

function extractDiagJson(stdout: string, stderr: string): string | undefined {
  const blobs = [stdout, stderr, `${stdout}\n${stderr}`];
  for (const text of blobs) {
    const i = text.indexOf("{");
    const j = text.lastIndexOf("}");
    if (i < 0 || j <= i) continue;
    const json = text.slice(i, j + 1);
    if (json.includes("\"diagnostics\"")) {
      return json;
    }
  }
  return undefined;
}

function normalizeDiagSchemaSignature(parsed: unknown): string {
  if (!parsed || typeof parsed !== "object") return "invalid";
  const asRecord = parsed as Record<string, unknown>;
  const schema = asRecord.diag_schema ?? asRecord.schema;
  if (schema === undefined || schema === null) return "missing";
  if (typeof schema === "string" || typeof schema === "number" || typeof schema === "boolean") {
    return String(schema);
  }
  if (typeof schema === "object") {
    const obj = schema as Record<string, unknown>;
    const keys = Object.keys(obj).sort();
    const version = typeof obj.version === "string" || typeof obj.version === "number" ? String(obj.version) : "";
    if (version) return `${version}|keys:${keys.join(",")}`;
    return `keys:${keys.join(",")}`;
  }
  return "unsupported";
}

function resolveVitteBinary(doc: vscode.TextDocument): { bin: string; cwd: string } | undefined {
  const folder = vscode.workspace.getWorkspaceFolder(doc.uri)?.uri.fsPath;
  if (!folder) return undefined;
  const cfg = vscode.workspace.getConfiguration("vitte");
  const explicit = cfg.get<string>("compiler.path", "").trim();
  const root = cfg.get<string>("toolchain.root", "").trim() || cfg.get<string>("toolchainPath", "").trim();
  const candidates = [
    explicit ? expandToolPath(explicit, folder) : "",
    root ? path.join(expandToolPath(root, folder), "bin", compilerName("vitte")) : "",
    ...workspaceBinaryRoots([folder]).map(binRoot => path.join(binRoot, compilerName("vitte"))),
    findExecutableOnPath("vitte"),
  ].filter((candidate): candidate is string => typeof candidate === "string" && candidate.length > 0);
  const bin = candidates.find(candidate => fs.existsSync(candidate));
  if (!bin) return undefined;
  return { bin, cwd: folder };
}

function compilerName(name: string): string {
  if (process.platform !== "win32") return name;
  return name.toLowerCase().endsWith(".exe") ? name : `${name}.exe`;
}

function findExecutableOnPath(name: string): string | undefined {
  const pathEnv = process.env.PATH ?? "";
  const exts = process.platform === "win32"
    ? (process.env.PATHEXT ?? ".EXE;.CMD;.BAT").split(";")
    : [""];
  for (const dir of pathEnv.split(path.delimiter)) {
    if (!dir) continue;
    for (const ext of exts) {
      const candidate = path.join(dir, name + ext);
      if (fs.existsSync(candidate)) return candidate;
    }
  }
  return undefined;
}

function parseVitteTextDiagnostics(
  doc: vscode.TextDocument,
  text: string,
  maxDiagnostics: number,
): vscode.Diagnostic[] {
  const diagnostics: vscode.Diagnostic[] = [];
  const docPath = path.normalize(doc.uri.fsPath);
  const docBase = path.basename(docPath);
  const rx = /^(.*?):(\d+):(\d+):\s+([A-Z][A-Z0-9_-]*):\s+(.*)$/gm;
  let match: RegExpExecArray | null;
  while ((match = rx.exec(text)) !== null && diagnostics.length < maxDiagnostics) {
    const file = match[1] ?? "";
    const normalizedFile = path.normalize(file);
    if (normalizedFile !== docPath && path.basename(normalizedFile) !== docBase) continue;
    const line = Math.max(0, Number(match[2]) - 1);
    const character = Math.max(0, Number(match[3]) - 1);
    const code = normalizeDiagCode(match[4]);
    const message = (match[5] ?? "Compiler diagnostic").trim();
    const range = new vscode.Range(line, character, line, character + 1);
    const diag = new vscode.Diagnostic(range, message, textDiagnosticSeverity(code));
    const parseCode = prefixedDiagCode("COMPILER", code || "UNKNOWN");
    const target = diagnosticDocUri(code);
    diag.code = target ? { value: parseCode, target } : parseCode;
    diag.source = "vitte";
    diagnostics.push(diag);
  }
  return diagnostics;
}

function textDiagnosticSeverity(code: string): vscode.DiagnosticSeverity {
  if (/^(W|WARN|WARNING)[A-Z0-9_-]*/.test(code)) return vscode.DiagnosticSeverity.Warning;
  if (/^(I|INFO|NOTE)[A-Z0-9_-]*/.test(code)) return vscode.DiagnosticSeverity.Information;
  if (/^(H|HELP)[A-Z0-9_-]*/.test(code)) return vscode.DiagnosticSeverity.Hint;
  return vscode.DiagnosticSeverity.Error;
}

function runLiveSyntaxDiagnosticsNow(doc: vscode.TextDocument, seq: number): void {
  if (!syntaxLintCollection || !shouldRunLiveSyntaxDiagnostics(doc)) return;
  const resolved = resolveVitteBinary(doc);
  if (!resolved) return;

  const cfg = vscode.workspace.getConfiguration("vitte");
  const lang = cfg.get<string>("lang", "en");
  const helpSource = cfg.get<DiagnosticHelpSource>("diagnostics.helpSource", "auto");
  const explainTimeoutMs = Math.max(100, Math.min(5000, cfg.get<number>("diagnostics.explainTimeoutMs", 450)));
  const args = ["check", "--diagnostic=json", "--no-color", doc.uri.fsPath];
  const key = doc.uri.toString();
  const prev = syntaxLintProcByDoc.get(key);
  if (prev && !prev.killed) {
    try { prev.kill(); } catch { /* noop */ }
  }

  syntaxParsingInFlight += 1;
  applyStatusBar();
  const maxDiag = Math.max(1, cfg.get<number>("syntax.maxDiagnosticsPerFile", 200));
  const firstErrorMode = cfg.get<SyntaxFirstErrorMode>("syntax.firstErrorMode", "line");
  const child = cp.spawn(resolved.bin, args, { cwd: resolved.cwd, shell: false });
  syntaxLintProcByDoc.set(key, child);
  let stdout = "";
  let stderr = "";
  const timeout = setTimeout(() => {
    try { child.kill(); } catch { /* noop */ }
  }, 4000);
  child.stdout?.on("data", (b: Buffer) => { stdout += b.toString(); });
  child.stderr?.on("data", (b: Buffer) => { stderr += b.toString(); });
  child.on("close", () => {
    clearTimeout(timeout);
    if (syntaxLintProcByDoc.get(key) === child) {
      syntaxLintProcByDoc.delete(key);
    }
    syntaxParsingInFlight = Math.max(0, syntaxParsingInFlight - 1);
    applyStatusBar();
    if ((syntaxLintSeq.get(key) ?? 0) !== seq || doc.isDirty) return;
    if (!syntaxLintCollection) return;

    const payload = extractDiagJson(stdout, stderr);
    if (!payload) {
      const diagnostics = parseVitteTextDiagnostics(doc, `${stdout}\n${stderr}`, maxDiag);
      syntaxLintCollection.set(doc.uri, dedupeDiagnosticsBySpanCodeSource(diagnostics));
      return;
    }
    try {
      const parsed = JSON.parse(payload) as {
        diag_schema?: unknown;
        diagnostics?: {
          severity?: string;
          code?: string | number;
          message?: string;
          start?: number;
          end?: number;
        }[];
      };
      const diagSchemaSignature = normalizeDiagSchemaSignature(parsed);
      if (diagSchemaSignature !== lastDiagSchemaSignature) {
        lastDiagSchemaSignature = diagSchemaSignature;
        obsLog("diagnostics.diagSchema.signature", "info", {
          signature: diagSchemaSignature,
          diagnosticsCount: Array.isArray(parsed.diagnostics) ? parsed.diagnostics.length : 0,
        });
      }
      const incoming = normalizeCompilerDiagnostics(parsed, doc.getText(), doc.uri.fsPath, resolved.cwd);
      const diagnostics: vscode.Diagnostic[] = [];
      const strictSeen = new Set<string>();
      const firstErrorLine = new Set<number>();
      let firstErrorInFileSeen = false;
      let lastError: vscode.Diagnostic | undefined;
      for (const d of incoming) {
        if (diagnostics.length >= maxDiag) break;
        const start = Math.max(0, Number.isFinite(d.start) ? Number(d.start) : 0);
        const endRaw = Number.isFinite(d.end) ? Number(d.end) : start + 1;
        const end = Math.max(start + 1, endRaw);
        const range = new vscode.Range(doc.positionAt(start), doc.positionAt(end));
        const baseMessage = typeof d.message === "string" && d.message.length > 0 ? d.message : "Syntax error";
        const severity = mapDiagSeverity(d.severity);
        const severityRaw = normalizeDiagSeverityToken(d.severity);
        const code = normalizeDiagCode(d.code);
        const parseCode = prefixedDiagCode("PARSE", code || "UNKNOWN");
        const sourceKey = "vitte";
        const dedupeKey = strictDiagnosticDedupKey(range, sourceKey, parseCode);
        if (strictSeen.has(dedupeKey)) continue;
        strictSeen.add(dedupeKey);
        const baseCode = explainableDiagCode(code);
        const explainHelp = baseCode ? resolveDiagnosticHelp(baseCode, lang, resolved, helpSource, explainTimeoutMs) : undefined;
        const message = baseCode ? formatVitteDiagnosticMessage(baseMessage, baseCode, explainHelp) : baseMessage;

        // Avoid cascade noise: configurable first parser error policy.
        if (severity === vscode.DiagnosticSeverity.Error) {
          if (firstErrorMode === "file") {
            if (firstErrorInFileSeen) continue;
            firstErrorInFileSeen = true;
          } else {
            if (firstErrorLine.has(range.start.line)) continue;
            firstErrorLine.add(range.start.line);
          }
        }

        if ((severityRaw === "note" || severityRaw === "help") && lastError) {
          const related = new vscode.DiagnosticRelatedInformation(
            new vscode.Location(doc.uri, range),
            message,
          );
          lastError.relatedInformation = [...(lastError.relatedInformation ?? []), related];
          continue;
        }

        const diag = new vscode.Diagnostic(range, message, severity);
        const target = baseCode ? diagnosticDocUri(baseCode) : undefined;
        diag.code = target ? { value: parseCode, target } : parseCode;
        diag.source = sourceKey;
        diagnostics.push(diag);
        if (severity === vscode.DiagnosticSeverity.Error) {
          lastError = diag;
        }
      }
      syntaxLintCollection.set(doc.uri, dedupeDiagnosticsBySpanCodeSource(diagnostics));
    } catch (parseErr) {
      output.appendLine(`[syntax.live] invalid diagnostic json: ${String(parseErr)}`);
      syntaxLintCollection.set(doc.uri, []);
    }
  });
  child.on("error", (spawnErr) => {
    clearTimeout(timeout);
    if (syntaxLintProcByDoc.get(key) === child) {
      syntaxLintProcByDoc.delete(key);
    }
    syntaxParsingInFlight = Math.max(0, syntaxParsingInFlight - 1);
    applyStatusBar();
    if (process.env.VITTE_DEBUG_LIVE_SYNTAX === "1") {
      output.appendLine(`[syntax.live] spawn error: ${String(spawnErr)}`);
    }
  });
}

function scheduleLiveSyntaxDiagnostics(doc: vscode.TextDocument, reason: "open" | "change" | "save" | "config" = "change"): void {
  if (!syntaxLintCollection) return;
  const key = doc.uri.toString();
  const prev = syntaxLintTimers.get(key);
  if (prev) clearTimeout(prev);
  const seq = (syntaxLintSeq.get(key) ?? 0) + 1;
  syntaxLintSeq.set(key, seq);
  if (!shouldRunLiveSyntaxDiagnostics(doc)) {
    syntaxLintCollection.delete(doc.uri);
    return;
  }
  const cfg = vscode.workspace.getConfiguration("vitte");
  const onSaveOnly = cfg.get<boolean>("syntax.liveDiagnosticsOnSaveOnly", false);
  if (onSaveOnly && reason !== "save") {
    return;
  }
  if (!isLiveSyntaxTarget(doc) && reason !== "save") {
    return;
  }
  const maxLines = Math.max(1, cfg.get<number>("syntax.maxLiveFileLines", 5000));
  if (doc.lineCount > maxLines) {
    syntaxLintCollection.delete(doc.uri);
    return;
  }
  const delayCfg = Math.max(60, cfg.get<number>("syntax.liveDiagnosticsDelayMs", 120));
  const typing = typingSpeedByDoc.get(key)?.intervalEwmaMs ?? 180;
  const delayMs = reason === "save"
    ? 60
    : (typing < 170 ? Math.max(300, delayCfg) : delayCfg);
  const timer = setTimeout(() => {
    syntaxLintTimers.delete(key);
    runLiveSyntaxDiagnosticsNow(doc, seq);
  }, delayMs);
  syntaxLintTimers.set(key, timer);
}

function updateEditorLint(doc: vscode.TextDocument): void {
  if (!editorLintCollection) return;
  const cfg = vscode.workspace.getConfiguration("vitte");
  if (!cfg.get<boolean>("features.lint", true)) {
    editorLintCollection.delete(doc.uri);
    return;
  }
  if (!isVitteDocument(doc)) {
    editorLintCollection.delete(doc.uri);
    return;
  }
  const lintCfg = cfg.get<{ maxLineLength?: number; allowTabs?: boolean; allowTrailingWhitespace?: boolean }>("lint") ?? {};
  const maxLineLength = typeof lintCfg.maxLineLength === "number" ? lintCfg.maxLineLength : 120;
  const allowTabs = lintCfg.allowTabs === true;
  const allowTrailing = lintCfg.allowTrailingWhitespace === true;

  const diagnostics: vscode.Diagnostic[] = [];
  const lines = doc.getText().split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line === undefined) continue;
    if (!allowTabs && line.includes("\t")) {
      const idx = line.indexOf("\t");
      const diag = new vscode.Diagnostic(
        new vscode.Range(i, idx, i, idx + 1),
        "Tabulation détectée. Utiliser des espaces.",
        vscode.DiagnosticSeverity.Warning
      );
      diag.code = prefixedDiagCode("LINT", "TABS");
      diagnostics.push(diag);
    }
    if (!allowTrailing) {
      const m = /[ \t]+$/.exec(line);
      if (m) {
        const start = m.index ?? Math.max(0, line.length - m[0].length);
        const trailing = new vscode.Diagnostic(
          new vscode.Range(i, start, i, line.length),
          "Espaces en fin de ligne.",
          vscode.DiagnosticSeverity.Hint
        );
        trailing.tags = [vscode.DiagnosticTag.Unnecessary];
        trailing.code = prefixedDiagCode("LINT", "TRAILING_WHITESPACE");
        diagnostics.push(trailing);
      }
    }
    if (maxLineLength > 0 && line.length > maxLineLength) {
      const diag = new vscode.Diagnostic(
        new vscode.Range(i, maxLineLength, i, line.length),
        `Ligne trop longue (${line.length} > ${maxLineLength}).`,
        vscode.DiagnosticSeverity.Hint
      );
      diag.code = prefixedDiagCode("LINT", "LINE_LENGTH");
      diagnostics.push(diag);
    }
  }
  diagnostics.push(...buildBracketDiagnostics(lines));
  for (const d of diagnostics) {
    d.source = "vitte";
  }
  editorLintCollection.set(doc.uri, dedupeDiagnosticsBySpanCodeSource(diagnostics));
}

function buildBracketDiagnostics(lines: string[]): vscode.Diagnostic[] {
  const diagnostics: vscode.Diagnostic[] = [];
  const stack: { char: "(" | "[" | "{"; line: number; col: number }[] = [];
  const pairs: Record<string, "(" | "[" | "{"> = { ")": "(", "]": "[", "}": "{" };
  const openers = new Set(["(", "[", "{"]);
  const closers = new Set([")", "]", "}"]);

  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const rawLine = lines[lineIndex] ?? "";
    const codeLine = normalizeForBracketScan(rawLine);
    for (let col = 0; col < codeLine.length; col++) {
      const char = codeLine[col];
      if (!char) continue;
      if (openers.has(char)) {
        stack.push({ char: char as "(" | "[" | "{", line: lineIndex, col });
        continue;
      }
      if (!closers.has(char)) continue;
      const expected = pairs[char];
      const top = stack[stack.length - 1];
      if (!top || top.char !== expected) {
        const diag = new vscode.Diagnostic(
          new vscode.Range(lineIndex, col, lineIndex, col + 1),
          `Parenthèse/accolade fermante inattendue "${char}".`,
          vscode.DiagnosticSeverity.Error
        );
        diag.code = prefixedDiagCode("BRACKETS", "UNEXPECTED_CLOSER");
        diagnostics.push(diag);
        continue;
      }
      stack.pop();
    }
  }

  for (const entry of stack) {
    const expected = entry.char === "(" ? ")" : entry.char === "[" ? "]" : "}";
    const diag = new vscode.Diagnostic(
      new vscode.Range(entry.line, entry.col, entry.line, entry.col + 1),
      `Parenthèse/accolade ouvrante "${entry.char}" non fermée (attendu: "${expected}").`,
      vscode.DiagnosticSeverity.Error
    );
    diag.code = prefixedDiagCode("BRACKETS", "UNCLOSED_OPENER");
    diagnostics.push(diag);
  }
  return diagnostics;
}

function normalizeForBracketScan(line: string): string {
  let result = "";
  let inSingle = false;
  let inDouble = false;
  let inTemplate = false;
  let escaped = false;

  for (let i = 0; i < line.length; i++) {
    const ch = line[i] ?? "";
    const next = i + 1 < line.length ? (line[i + 1] ?? "") : "";

    if (!inSingle && !inDouble && !inTemplate && ch === "/" && next === "/") {
      break;
    }
    if (escaped) {
      escaped = false;
      result += " ";
      continue;
    }
    if (ch === "\\") {
      escaped = true;
      result += " ";
      continue;
    }
    if (!inDouble && !inTemplate && ch === "'") {
      inSingle = !inSingle;
      result += " ";
      continue;
    }
    if (!inSingle && !inTemplate && ch === "\"") {
      inDouble = !inDouble;
      result += " ";
      continue;
    }
    if (!inSingle && !inDouble && ch === "`") {
      inTemplate = !inTemplate;
      result += " ";
      continue;
    }
    result += inSingle || inDouble || inTemplate ? " " : ch;
  }

  return result;
}

function summarizeWorkspaceEdit(edit: vscode.WorkspaceEdit): { fileCount: number; editCount: number } {
  const entries = edit.entries();
  let editCount = 0;
  for (const [, edits] of entries) editCount += edits.length;
  return { fileCount: entries.length, editCount };
}

function summarizeWorkspaceEditDetailed(edit: vscode.WorkspaceEdit): { fileCount: number; editCount: number; details: string[] } {
  const base = summarizeWorkspaceEdit(edit);
  const details: string[] = [];
  for (const [uri, edits] of edit.entries()) {
    const rel = vscode.workspace.asRelativePath(uri, false);
    details.push(`${rel}: ${edits.length}`);
    if (details.length >= 12) break;
  }
  return { ...base, details };
}

async function detectRenameConflicts(edit: vscode.WorkspaceEdit, newName: string): Promise<string[]> {
  const conflictFiles: string[] = [];
  const declRx = new RegExp(`\\b(?:proc|fn|entry|let|const|static|type|struct|form|trait|enum|union)\\s+${newName}\\b`);
  for (const [uri] of edit.entries()) {
    try {
      const doc = await vscode.workspace.openTextDocument(uri);
      if (!["vitte", "vit"].includes(doc.languageId)) continue;
      if (declRx.test(doc.getText())) {
        conflictFiles.push(vscode.workspace.asRelativePath(uri, false));
      }
    } catch {
      // ignore unreadable file
    }
  }
  return conflictFiles;
}

async function writeRenameReport(data: {
  ts: string;
  oldName: string;
  newName: string;
  fileCount: number;
  editCount: number;
  applied: boolean;
  error?: string;
  files: string[];
}): Promise<void> {
  const dir = safeWorkspaceSubPath(".vitte-cache", "rename");
  if (!dir) return;
  try {
    await fs.promises.mkdir(dir, { recursive: true });
    const file = path.join(dir, `rename-${Date.now()}.json`);
    await fs.promises.writeFile(file, `${JSON.stringify(data, null, 2)}\n`, "utf8");
  } catch {
    // ignore report write errors
  }
}

async function captureWorkspaceEditSnapshots(edit: vscode.WorkspaceEdit): Promise<Map<string, string>> {
  const snapshots = new Map<string, string>();
  for (const [uri] of edit.entries()) {
    try {
      const doc = await vscode.workspace.openTextDocument(uri);
      snapshots.set(uri.toString(), doc.getText());
    } catch {
      // ignore unreadable files
    }
  }
  return snapshots;
}

async function rollbackWorkspaceEditSnapshots(snapshots: Map<string, string>): Promise<void> {
  if (snapshots.size === 0) return;
  const rollback = new vscode.WorkspaceEdit();
  for (const [uriText, original] of snapshots) {
    const uri = vscode.Uri.parse(uriText);
    try {
      const doc = await vscode.workspace.openTextDocument(uri);
      const full = new vscode.Range(0, 0, doc.lineCount, 0);
      rollback.replace(uri, full, original);
    } catch {
      // ignore unavailable files
    }
  }
  await vscode.workspace.applyEdit(rollback);
}

function sleep(ms: number): Promise<void> { return new Promise(res => setTimeout(res, ms)); }

function updateStatusText(editor?: vscode.TextEditor): void {
  const lang = editor?.document?.languageId;
  if (lang && LANGUAGE_SET.has(lang)) {
    setStatusLanguageSuffix(lang);
    return;
  }
  setStatusLanguageSuffix("");
}

/* -------------------------------- Debug demo ------------------------------ */

async function runDebugCurrentFile(): Promise<void> {
  const editor = vscode.window.activeTextEditor; if (!editor) return;
  const folder = vscode.workspace.workspaceFolders?.[0];
  const cfg: vscode.DebugConfiguration = {
    type: "vitte",
    name: "Vitte: Launch current file",
    request: "launch",
    program: editor.document.fileName,
    cwd: folder?.uri.fsPath ?? path.dirname(editor.document.fileName),
    stopOnEntry: true,
    args: []
  };
  await vscode.debug.startDebugging(folder, cfg);
}

async function attachDebugServer(): Promise<void> {
  const portStr = await vscode.window.showInputBox({ prompt: "Port du serveur Vitte", value: "9333" });
  if (!portStr) return;
  const host = await vscode.window.showInputBox({ prompt: "Hôte du serveur DAP", value: "127.0.0.1" });
  if (!host) return;
  const folder = vscode.workspace.workspaceFolders?.[0];
  const port = Number.parseInt(portStr, 10);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    void vscode.window.showErrorMessage("Port DAP invalide : la valeur doit être comprise entre 1 et 65535.");
    return;
  }
  const cfg: vscode.DebugConfiguration = {
    type: "vitte",
    name: "Vitte: Attach to DAP server",
    request: "attach",
    adapterTransport: "tcp",
    host,
    port,
  };
  await vscode.debug.startDebugging(folder, cfg);
}
