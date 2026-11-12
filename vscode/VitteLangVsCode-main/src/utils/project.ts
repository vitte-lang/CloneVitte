import * as fs from 'node:fs';
import * as path from 'node:path';

export const BUILD_PROFILES = ['default', 'debug', 'release', 'size', 'sanitize', 'freestanding'] as const;

export function compilerProfile(profile = 'default'): string {
  if (profile === 'dev' || profile === 'test') return 'debug';
  if (profile === 'bench') return 'release';
  return (BUILD_PROFILES as readonly string[]).includes(profile) ? profile : 'default';
}

/** Locate the compiler checkout even when VS Code opens a nested folder. */
export function findCompilerRepository(start: string): string | undefined {
  let current = path.resolve(start);
  while (true) {
    if (['Makefile', 'grammar/vitte.ebnf', 'src/bootstrap/cli/command_spec.def'].every(p => fs.existsSync(path.join(current, p)))) return current;
    const parent = path.dirname(current);
    if (parent === current) return undefined;
    current = parent;
  }
}

export function workspaceBinaryRoots(roots: readonly string[]): string[] {
  return [...new Set(roots.flatMap(root => {
    const repository = findCompilerRepository(root);
    return [
      ...(repository ? [path.join(repository, 'build', 'bin')] : []),
      path.join(root, 'build', 'bin'), path.join(root, '.vitte', 'bin'),
      path.join(root, 'vitte', 'bin'), path.join(root, '.bin'), path.join(root, 'tools', 'vitte', 'bin'),
    ];
  }))];
}

export function expandToolPath(value: string, workspaceRoot: string, toolchainRoot?: string): string {
  const expanded = value.replace(/\$\{workspaceFolder}/g, workspaceRoot)
    .replace(/\$\{env:([^}]+)}/g, (_match: string, key: string) => process.env[key] ?? '');
  return path.isAbsolute(expanded) ? expanded : path.resolve(toolchainRoot ?? workspaceRoot, expanded);
}

export interface ProjectCommand {
  bin: string;
  args: string[];
  cwd: string;
  outputDir?: string;
}

export function projectCommand(options: {
  root: string; compiler: string; action: 'build' | 'run' | 'test' | 'clean';
  input?: string | undefined; currentFileOnly?: boolean | undefined;
  profile?: string | undefined; incremental?: boolean | undefined;
}): ProjectCommand {
  const { root, compiler, action, input } = options;
  const repository = findCompilerRepository(root);
  if (repository && action !== 'run' && !options.currentFileOnly) {
    const profile = compilerProfile(options.profile);
    const mode = profile === 'release' || profile === 'size' ? 'release' : profile === 'sanitize' ? 'asan' : 'debug';
    return { bin: 'make', args: [action === 'build' ? 'all' : action, `MODE=${mode}`], cwd: repository };
  }
  if (action === 'clean') return { bin: '', args: [], cwd: root, outputDir: path.join(root, 'build', 'vscode') };
  if (!input) throw new Error('Aucun fichier Vitte actif ou src/main.vit trouvé.');
  const args = [action === 'build' ? 'compile' : action === 'test' ? 'check' : 'run', input];
  if (options.profile && options.profile !== 'default') args.push(`--profile=${compilerProfile(options.profile)}`);
  if (options.incremental) args.push('--incremental');
  if (action === 'build') {
    const outputDir = path.join(root, 'build', 'vscode');
    args.push('-o', path.join(outputDir, path.parse(input).name + (process.platform === 'win32' ? '.exe' : '')));
    return { bin: compiler, args, cwd: root, outputDir };
  }
  return { bin: compiler, args, cwd: root };
}

export function projectSource(name: string): string {
  return `space ${name};\n\nproc main() -> int {\n    return 0;\n}\n`;
}

// The current configuration reader accepts flat keys, without TOML sections.
export const PROJECT_CONFIG = 'profile = "debug"\nbackend = "c17"\n';

export function workspaceConfiguration(root: string, compiler = 'vitte') {
  const repository = findCompilerRepository(root);
  const source = repository ? '${workspaceFolder}/' + path.relative(root, path.join(repository, 'examples/hello.vit')).split(path.sep).join('/') : '${workspaceFolder}/src/main.vit';
  const processTask = (label: string, command: string, args: string[], group = 'build') => ({
    label, type: 'process', command, args, group, problemMatcher: [], options: { cwd: repository ? '${workspaceFolder}/' + path.relative(root, repository).split(path.sep).join('/') : '${workspaceFolder}' },
  });
  const tasks = repository ? [
    processTask('vitte: build compiler', 'make', ['all']),
    processTask('vitte: bootstrap', 'make', ['bootstrap']),
    processTask('vitte: test native frontend', 'make', ['test-native-frontend-subset'], 'test'),
    processTask('vitte: test grammar', 'make', ['test-grammar'], 'test'),
    processTask('vitte: test', 'make', ['test'], 'test'),
  ] : [processTask('vitte: compile', compiler, ['compile', source])];
  tasks.push(processTask('vitte: check current file', compiler, ['check', '--diagnostic=json', '${file}']));
  tasks.push(processTask('vitte: run current file', compiler, ['run', '${file}']));
  return {
    tasks: { version: '2.0.0', tasks },
    launch: { version: '0.2.0', configurations: [
      { name: 'Vitte: Launch current file', type: 'vitte', request: 'launch', program: '${file}', cwd: '${workspaceFolder}', args: [], nativeBackend: true },
      { name: 'Vitte: Launch project entry', type: 'vitte', request: 'launch', program: source, cwd: '${workspaceFolder}', args: [], nativeBackend: true },
    ] },
  };
}
