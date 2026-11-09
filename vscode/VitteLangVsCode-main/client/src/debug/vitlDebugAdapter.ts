import * as vscode from "vscode";

/** À appeler depuis activate() */
export function registerVitlDebugAdapter(context: vscode.ExtensionContext) {
  const factory = new VitlDebugAdapterFactory();
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("vitl", factory),
    vscode.debug.registerDebugConfigurationProvider("vitl", new VitlDebugConfigProvider()),
    factory
  );
}

/** Fournit des valeurs par défaut et validation légère des configs launch.json */
class VitlDebugConfigProvider implements vscode.DebugConfigurationProvider {
  resolveDebugConfiguration(
    _folder: vscode.WorkspaceFolder | undefined,
    config: vscode.DebugConfiguration
  ): vscode.ProviderResult<vscode.DebugConfiguration> {
    config.type ??= "vitl";
    config.request ??= "launch";
    config.name ??= "Vitte: Launch current file";
    config.program ??= "${file}";
    config.cwd ??= "${workspaceFolder}";
    config.stopOnEntry ??= true;
    config.args ??= [];
    return config;
  }
}

/** Fabrique d’adaptateur inline (aucun process externe) */
export class VitlDebugAdapterFactory
  implements vscode.DebugAdapterDescriptorFactory, vscode.Disposable {
  createDebugAdapterDescriptor(
    _session: vscode.DebugSession
  ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    const InlineImpl = (vscode as any).DebugAdapterInlineImplementation;
    if (typeof InlineImpl === "function") {
      return new InlineImpl(new VitlInlineAdapter()) as vscode.DebugAdapterDescriptor;
    }
    vscode.window.showErrorMessage(
      "Inline debug adapter non supporté par cette version de VS Code. Mettez à jour VS Code et la dépendance 'vscode'."
    );
    return undefined;
  }
  dispose() { /* no-op */ }
}

/** Adaptateur DAP ultra fourni */
class VitlInlineAdapter implements vscode.DebugAdapter {
  private readonly emitter = new vscode.EventEmitter<vscode.DebugProtocolMessage>();
  readonly onDidSendMessage = this.emitter.event;

  private breakpoints: { [path: string]: any[] } = {};
  private vars: Record<number, any[]> = {
    1: [{ name: "counter", value: "0", type: "int", variablesReference: 0 }],
    2: [{ name: "VERSION", value: "0.1.1", type: "string", variablesReference: 0 }]
  };

  handleMessage(msg: any): void {
    const cmd = msg?.command as string | undefined;

    switch (cmd) {
      /* ----------------- Initialisation ----------------- */
      case "initialize":
        this.respond(msg, true, {
          supportsConfigurationDoneRequest: true,
          supportsTerminateRequest: true,
          supportsRestartRequest: true,
          supportsEvaluateForHovers: true,
          supportsConditionalBreakpoints: true,
          supportsLogPoints: true,
          supportsBreakpointLocationsRequest: true,
          supportsStepBack: true,
          supportsCompletionsRequest: true,
          supportsExceptionInfoRequest: true,
          supportsLoadedSourcesRequest: true,
          supportsSetVariable: true,
          supportsDataBreakpoints: true,
          supportsModulesRequest: true,
          supportsDisassembleRequest: true,
          supportsReadMemoryRequest: true
        });
        this.event("capabilities", { supportsProgressReporting: true });
        this.event("initialized");
        break;

      case "configurationDone":
        this.respond(msg, true);
        break;

      /* ----------------- Lancement ----------------- */
      case "launch":
        this.event("process", {
          name: "Vitte Program",
          systemProcessId: 1234,
          isLocalProcess: true,
          startMethod: "launch"
        });
        this.event("thread", { reason: "started", threadId: 1 });
        if (msg?.arguments?.stopOnEntry) {
          this.event("stopped", { reason: "entry", threadId: 1, allThreadsStopped: true });
        } else {
          this.event("continued", { threadId: 1, allThreadsContinued: true });
        }
        this.respond(msg, true);
        break;

      /* ----------------- Contrôle exécution ----------------- */
      case "continue":
      case "next":
      case "stepIn":
      case "stepOut":
      case "stepBack":
      case "pause":
        this.event("continued", { threadId: 1, allThreadsContinued: true });
        this.respond(msg, true, { allThreadsContinued: true });
        break;

      /* ----------------- Threads & Stack ----------------- */
      case "threads":
        this.respond(msg, true, { threads: [{ id: 1, name: "Vitte Main Thread" }] });
        break;

      case "stackTrace":
        this.respond(msg, true, {
          stackFrames: [{
            id: 1,
            name: "main",
            source: {
              name: msg?.arguments?.source?.name ?? "unknown.vit",
              path: msg?.arguments?.source?.path
            },
            line: 1,
            column: 1
          }],
          totalFrames: 1
        });
        break;

      case "scopes":
        this.respond(msg, true, {
          scopes: [
            { name: "Locals", variablesReference: 1, expensive: false },
            { name: "Globals", variablesReference: 2, expensive: true }
          ]
        });
        break;

      case "variables": {
        const ref = msg?.arguments?.variablesReference;
        const vars = this.vars[ref] ?? [];
        this.respond(msg, true, { variables: vars });
        break;
      }

      case "setVariable": {
        const ref = msg?.arguments?.variablesReference;
        const name = msg?.arguments?.name;
        const value = msg?.arguments?.value;
        const list = this.vars[ref];
        if (list) {
          const v = list.find(x => x.name === name);
          if (v) v.value = value;
        }
        this.respond(msg, true, { value });
        break;
      }

      /* ----------------- Breakpoints ----------------- */
      case "setBreakpoints": {
        const src = msg?.arguments?.source?.path ?? "unknown";
        const req = Array.isArray(msg?.arguments?.breakpoints) ? msg.arguments.breakpoints : [];
        const bps = req.map((b: any, i: number) => ({
          id: i + 1,
          verified: true,
          line: b?.line ?? 1,
          condition: b?.condition,
          logMessage: b?.logMessage
        }));
        this.breakpoints[src] = bps;
        this.respond(msg, true, { breakpoints: bps });
        break;
      }

      case "breakpointLocations":
        this.respond(msg, true, {
          breakpoints: [{ line: 1 }, { line: 2 }, { line: 10 }]
        });
        break;

      /* ----------------- Exceptions, Modules, Sources ----------------- */
      case "exceptionInfo":
        this.respond(msg, true, {
          exceptionId: "RuntimeError",
          description: "Fake runtime exception",
          breakMode: "always"
        });
        break;

      case "loadedSources":
        this.respond(msg, true, {
          sources: [
            { name: "main.vit", path: "/fake/path/main.vit" },
            { name: "lib.vitl", path: "/fake/path/lib.vitl" }
          ]
        });
        break;

      case "modules":
        this.respond(msg, true, {
          modules: [
            { id: "core", name: "Vitte Core Library", path: "/fake/path/core.vitl" },
            { id: "std", name: "Vitte Standard", path: "/fake/path/std.vitl" }
          ]
        });
        break;

      /* ----------------- Evaluate & Completions ----------------- */
      case "evaluate": {
        const expr = String(msg?.arguments?.expression ?? "");
        const ctx = msg?.arguments?.context;
        if (expr === "time()") {
          this.respond(msg, true, {
            result: new Date().toISOString(),
            variablesReference: 0
          });
        } else if (ctx === "hover") {
          this.respond(msg, true, { result: `hover info for ${expr}`, variablesReference: 0 });
        } else {
          this.respond(msg, true, { result: `expr[${expr}]`, variablesReference: 0 });
        }
        break;
      }

      case "completions":
        this.respond(msg, true, {
          targets: [
            { label: "print", type: "function", text: "print" },
            { label: "len", type: "function", text: "len" },
            { label: "time", type: "function", text: "time()" }
          ]
        });
        break;

      /* ----------------- Mémoire & désassemblage ----------------- */
      case "readMemory":
        this.respond(msg, true, {
          address: "0x1000",
          data: Buffer.from("HELLO").toString("base64"),
          unreadableBytes: 0
        });
        break;

      case "disassemble":
        this.respond(msg, true, {
          instructions: [
            { address: "0x1000", instruction: "LOAD R1, #42" },
            { address: "0x1004", instruction: "CALL print" }
          ]
        });
        break;

      /* ----------------- Fin & erreurs ----------------- */
      case "disconnect":
      case "terminate":
        this.event("exited", { exitCode: 0 });
        this.event("terminated");
        this.respond(msg, true);
        break;

      default:
        this.output(`Unknown request: ${cmd}`, "stderr");
        this.respond(msg, false, undefined, `Unknown command: ${cmd}`);
        break;
    }
  }

  dispose(): void { this.emitter.dispose(); }

  /* Helpers internes */
  private send(message: vscode.DebugProtocolMessage) { this.emitter.fire(message); }
  private respond(req: any, success: boolean, body?: any, message?: string) {
    this.send({
      seq: 0,
      type: "response",
      request_seq: req?.seq ?? 0,
      success,
      command: req?.command ?? "",
      message,
      body
    } as any);
  }
  private event(event: string, body?: any) {
    this.send({ seq: 0, type: "event", event, body } as any);
  }
  private output(text: string, category: "console" | "stdout" | "stderr" = "console") {
    this.event("output", { category, output: text + "\n" });
  }
}
