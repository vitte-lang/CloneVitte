import { cp, mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const root = new URL("../", import.meta.url);
await mkdir(new URL("out/", root), { recursive: true });
// Retain the legacy entry points while keeping the canonical out/src layout.
// Missing compiler output must fail packaging, on every supported platform.
await cp(fileURLToPath(new URL("server/out/src/server.js", root)), fileURLToPath(new URL("out/server.js", root)));
await cp(fileURLToPath(new URL("server/out/src/", root)), fileURLToPath(new URL("server/out/", root)), { recursive: true });
