const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');
const { findCompilerRepository, workspaceBinaryRoots, expandToolPath, projectCommand, workspaceConfiguration, projectSource, PROJECT_CONFIG, compilerProfile } = require('../out/utils/project');
const { normalizeCompilerDiagnostics } = require('../out/utils/compilerDiagnostics');
const extensionRoot = path.resolve(__dirname, '..');
const repo = findCompilerRepository(extensionRoot);
const nativeCompiler = repo && path.join(repo, 'build/bin', process.platform === 'win32' ? 'vitte.exe' : 'vitte');

function temporaryProject(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vitte-project-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return root;
}

test('compiler checkout detection works from nested folders without selecting a seed', (t) => {
  const root = temporaryProject(t);
  for (const file of ['Makefile', 'grammar/vitte.ebnf', 'src/bootstrap/cli/command_spec.def']) {
    fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true });
    fs.writeFileSync(path.join(root, file), '');
  }
  const nested = path.join(root, 'vscode', 'extension');
  assert.equal(findCompilerRepository(nested), root);
  assert.equal(workspaceBinaryRoots([nested])[0], path.join(root, 'build/bin'));
  assert.equal(findCompilerRepository(path.dirname(root)), undefined);
  assert.equal(expandToolPath('${workspaceFolder}/build/bin/vitte', root), path.join(root, 'build/bin/vitte'));
  assert.equal(expandToolPath('build/bin/vitte', root), path.join(root, 'build/bin/vitte'));
});

test('compiler repository builds and tests use Make, current-file checks use the CLI', { skip: !repo }, () => {
  for (const [action, target] of [['build', 'all'], ['test', 'test'], ['clean', 'clean']]) {
    const command = projectCommand({ root: extensionRoot, compiler: nativeCompiler, action });
    assert.equal(command.bin, 'make');
    assert.equal(command.args[0], target);
    assert.equal(command.cwd, repo);
  }
  const command = projectCommand({ root: repo, compiler: nativeCompiler, action: 'test', currentFileOnly: true, input: '/tmp/main.vit' });
  assert.deepEqual(command.args, ['check', '/tmp/main.vit']);
  assert.equal(command.bin, nativeCompiler);
});

test('application commands use canonical CLI options and isolate editor output', (t) => {
  const root = temporaryProject(t);
  const input = path.join(root, 'src/main.vit');
  const command = projectCommand({ root, compiler: 'vitte', action: 'build', input, profile: 'dev', incremental: true });
  assert.deepEqual(command.args.slice(0, 4), ['compile', input, '--profile=debug', '--incremental']);
  assert.equal(command.outputDir, path.join(root, 'build/vscode'));
  assert.equal(projectCommand({ root, compiler: 'vitte', action: 'clean' }).outputDir, path.join(root, 'build/vscode'));
  assert.equal(projectCommand({ root, compiler: 'vitte', action: 'run', input }).args[0], 'run');
  assert.equal(compilerProfile('bench'), 'release');
  assert.throws(() => projectCommand({ root, compiler: 'vitte', action: 'build' }), /fichier Vitte/);
});

test('workspace scaffolding uses process tasks and source debuggees', (t) => {
  const root = temporaryProject(t);
  const config = workspaceConfiguration(root, 'vitte with spaces');
  assert.ok(config.tasks.tasks.every(task => task.type === 'process' && Array.isArray(task.args)));
  assert.ok(config.tasks.tasks.every(task => ['compile', 'check', 'run'].includes(task.args[0])));
  for (const launch of config.launch.configurations) {
    assert.ok(launch.program.endsWith('.vit') || launch.program === '${file}');
    assert.deepEqual(launch.args, []);
    assert.equal(launch.nativeBackend, true);
  }
  assert.ok(!PROJECT_CONFIG.includes('[build]'));
  assert.ok(projectSource('demo').includes('proc main() -> int'));
  assert.ok(projectSource('demo').includes('return 0;'));
});

test('generated repository tasks reference actual Make targets and remain portable', { skip: !repo }, () => {
  const config = workspaceConfiguration(repo, '${workspaceFolder}/build/bin/vitte');
  const makefile = fs.readFileSync(path.join(repo, 'Makefile'), 'utf8');
  for (const task of config.tasks.tasks.filter(task => task.command === 'make')) {
    assert.match(makefile, new RegExp(`^${task.args[0]}:`, 'm'));
    assert.ok(task.options.cwd.startsWith('${workspaceFolder}'));
  }
  assert.ok(config.launch.configurations[1].program.endsWith('examples/hello.vit'));
});

test('native JSON diagnostics translate UTF-8 bytes to UTF-16 and exclude imported files', () => {
  const text = '// café 😀\nreturn missing;';
  const start = text.indexOf('missing');
  const position = { source: 'main.vit', start_byte: Buffer.byteLength(text.slice(0, start)), end_byte: Buffer.byteLength(text.slice(0, start + 7)) };
  const payload = { schema: 'vitte-diagnostics', version: 1, diagnostics: [
    { code: 'E0401', severity: 'error', message: 'unknown symbol', position },
    { code: 'E0401', position: { ...position, source: 'imported.vit' } },
  ] };
  const result = normalizeCompilerDiagnostics(payload, text, '/tmp/main.vit', '/tmp');
  assert.equal(result.length, 1);
  assert.equal(result[0].start, start);
  assert.equal(text.slice(result[0].start, result[0].end), 'missing');
  assert.equal(result[0].code, 'E0401');
  assert.equal(normalizeCompilerDiagnostics({ diagnostics: [{ start: 1, end: 3 }] }, text, '/tmp/main.vit', '/tmp')[0].end, 3);
});

test('new project config and source pass the actual Vitte compiler', { skip: !nativeCompiler || !fs.existsSync(nativeCompiler) }, (t) => {
  const root = temporaryProject(t);
  const source = path.join(root, 'main.vit');
  fs.writeFileSync(source, projectSource('editor_project'));
  fs.writeFileSync(path.join(root, 'vitte.toml'), PROJECT_CONFIG);
  const checked = spawnSync(nativeCompiler, ['check', '--diagnostic=json', source], { cwd: root, encoding: 'utf8', timeout: 10000 });
  assert.equal(checked.status, 0, checked.stdout + checked.stderr);
  const output = path.join(root, process.platform === 'win32' ? 'program.exe' : 'program');
  const compiled = spawnSync(nativeCompiler, ['compile', source, '-o', output], { cwd: root, encoding: 'utf8', timeout: 10000 });
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
  const executed = spawnSync(output, [], { cwd: root, encoding: 'utf8', timeout: 10000 });
  assert.equal(executed.status, 0, executed.stderr);
});

test('real compiler diagnostic envelopes identify the correct Unicode source span', { skip: !nativeCompiler || !fs.existsSync(nativeCompiler) }, (t) => {
  const root = temporaryProject(t);
  const file = path.join(root, 'main.vit');
  const text = '// français 😀\nspace editor;\nproc main() -> int { return missing_value; }\n';
  fs.writeFileSync(file, text);
  const run = spawnSync(nativeCompiler, ['check', '--diagnostic=json', '--no-color', file], { cwd: root, encoding: 'utf8', timeout: 10000 });
  assert.equal(run.status, 1, run.stdout + run.stderr);
  const payload = JSON.parse(run.stdout);
  assert.equal(payload.schema, 'vitte-diagnostics');
  const diagnostics = normalizeCompilerDiagnostics(payload, text, file, root);
  const unknown = diagnostics.find(diagnostic => diagnostic.code === 'E0401');
  assert.ok(unknown, JSON.stringify(payload));
  assert.equal(text.slice(unknown.start, unknown.end), 'missing_value');
});
