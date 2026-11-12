
# Security Policy

## Vitte Programming Language

Security is an important part of the Vitte programming language project.

This policy describes how to report vulnerabilities affecting the Vitte compiler, runtime, standard library, development tools, build infrastructure, and official distribution packages.

**Repository:** https://github.com/vitte-lang/vitte

**Maintainer:** Vitte Team

---

## Supported Versions

Security maintenance depends on the release lifecycle and available project resources.

| Version | Status | Security Support |
|---|---|---|
| 0.1.1 | Current release | Security reports accepted |
| 0.1.0 | Previous release | Best effort |
| Development branch | Active development | Security reports accepted |
| Older versions | Legacy | No guaranteed fixes |

Security support means that vulnerability reports are reviewed and evaluated. It does not guarantee a particular response time or that every reported issue will receive a patch.

Users are encouraged to use the latest validated release.

## Reporting a Vulnerability

**Please do not disclose suspected security vulnerabilities through public GitHub Issues, Pull Requests, or Discussions before coordinated disclosure.**

Use GitHub's private vulnerability reporting mechanism:

https://github.com/vitte-lang/vitte/security/advisories/new

When submitting a report, include:

1. A clear description of the vulnerability.
2. The affected Vitte version or commit.
3. The affected compiler component, library, or tool.
4. The operating system and architecture.
5. Steps required to reproduce the issue.
6. A minimal proof of concept, where appropriate.
7. The expected and observed behavior.
8. The potential security impact.
9. Relevant logs, diagnostics, or stack traces.
10. Any proposed mitigation or patch.

Do not include passwords, access tokens, private keys, or other sensitive information in vulnerability reports.

If private vulnerability reporting is unavailable, contact the maintainers through an established private channel rather than publishing exploit details.

## Scope

Security reports may concern the following components.

### Compiler

- Lexer and parser
- Abstract Syntax Tree (AST)
- Name resolution
- Semantic analysis
- Type checking
- HIR and MIR
- Borrow checking
- Constant evaluation
- Code generation
- Compiler backends
- Bootstrap and self-hosting

### Runtime and Standard Library

- Memory allocation
- Pointer operations
- Ownership and borrowing
- Buffer management
- Filesystem operations
- Input/output
- Foreign Function Interface (FFI)
- Application Binary Interface (ABI)
- Concurrency and synchronization
- Unsafe operations

### Build and Distribution

- Build scripts
- Installation scripts
- Package generation
- GitHub Actions
- Release automation
- Binary artifacts
- Software dependencies
- Supply-chain integrity
- Release metadata
- Software Bill of Materials (SBOM)

### Developer Tools

- Command-line interfaces
- Language Server Protocol implementation
- Editor integrations
- Shell completions
- Package and module management

## Vulnerability Classification

Security issues are evaluated according to their practical impact, exploitability, and affected environments.

| Severity | Examples |
|---|---|
| Critical | Remote code execution under realistic conditions, major supply-chain compromise |
| High | Exploitable memory corruption, privilege escalation, malicious package execution |
| Medium | Significant security boundary bypass, exploitable denial of service |
| Low | Limited-impact security weakness requiring restrictive conditions |
| Informational | Security hardening opportunity without demonstrated exploitability |

Severity is determined after investigation.

Where appropriate, CVSS may be used to help assess vulnerabilities.

## Compiler Security

The Vitte compiler processes source code that may originate from untrusted sources.

Security-sensitive areas include:

- Input validation
- Unicode processing
- Integer overflow
- Buffer bounds
- Pointer arithmetic
- Memory allocation
- Parser recursion
- Compiler resource exhaustion
- Intermediate representation validation
- Backend code generation
- External compiler invocation
- File and path handling

A compiler crash is not automatically a security vulnerability. Its classification depends on exploitability and impact.

## Memory Safety

Vitte aims to provide reliable memory handling while supporting systems-level programming.

Security testing should investigate:

- Out-of-bounds reads and writes
- Use-after-free
- Double-free
- Invalid pointer dereferencing
- Uninitialized memory access
- Alignment violations
- Integer overflow affecting memory operations
- Data races
- Ownership violations
- Incorrect lifetime handling

Memory safety guarantees depend on the implementation, compiler version, and use of unsafe operations.

No universal memory safety guarantee is implied by this policy.

## Supply-Chain Security

Official Vitte releases may include:

- Installation packages
- Portable archives
- SHA-256 checksums
- Release manifests
- SBOM files
- Signature metadata

Users should download release artifacts from:

https://github.com/vitte-lang/vitte/releases

For Vitte 0.1.1:

https://github.com/vitte-lang/vitte/releases/tag/v0.1.1

### Artifact Verification

Where checksum files are available, verify downloaded artifacts before installation.

Example on macOS:

```bash
shasum -a 256 vitte-0.1.1-macos-arm64.pkg
```

Example on Linux:

```bash
sha256sum vitte_0.1.1_amd64.deb
```

Compare the calculated checksum against the official release checksum.

Checksums detect file changes but do not independently authenticate the publisher. Cryptographic signatures should be verified separately when supported.

### Software Bill of Materials

Vitte release artifacts may provide SBOM files in SPDX and CycloneDX formats.

These files help document software components and dependencies.

An SBOM does not guarantee the absence of vulnerabilities or the completeness of dependency information.

## Vulnerability Handling Process

Security reports should follow this process:

1. Private submission
2. Initial review
3. Reproduction and technical analysis
4. Impact and severity assessment
5. Identification of affected versions
6. Development of a mitigation or fix
7. Security regression testing
8. Release preparation
9. Coordinated disclosure, where appropriate
10. Publication of a security advisory when warranted

The maintainers may request additional technical information during the investigation.

Response and remediation timelines depend on severity, available resources, and technical complexity.

## Coordinated Disclosure

Researchers are encouraged to allow maintainers reasonable time to investigate and address vulnerabilities before public disclosure.

The project aims to coordinate disclosure responsibly and avoid unnecessarily exposing users to active exploitation.

No fixed disclosure deadline is guaranteed by this policy.

## Security Testing

The project encourages appropriate security validation, including:

- Static analysis
- Dynamic analysis
- Fuzz testing
- AddressSanitizer
- UndefinedBehaviorSanitizer
- Regression testing
- Compiler input validation
- Dependency auditing
- Release artifact verification

Security tests should be performed on systems and resources that the researcher owns or is explicitly authorized to test.

## Security Research Guidelines

We welcome responsible, good-faith security research.

Researchers should:

- Avoid accessing data belonging to other users.
- Avoid disrupting production infrastructure.
- Avoid destructive testing.
- Avoid exposing credentials or private information.
- Report suspected vulnerabilities privately.
- Provide reproducible technical information.
- Respect applicable laws and authorization boundaries.

This policy does not establish a bug bounty program or promise financial compensation.

## Security Advisories

Confirmed vulnerabilities may be documented through GitHub Security Advisories.

Advisories may include:

- Affected versions
- Vulnerability description
- Severity assessment
- Impact
- Mitigations
- Patched versions
- References
- CVE identifiers, where applicable

Security advisories are available at:

https://github.com/vitte-lang/vitte/security/advisories

## Security Updates

Security fixes may be distributed through:

- Patch releases
- Updated installation packages
- Source code updates
- Dependency updates
- Security advisories

Users should review official release notes and security announcements before upgrading.

## Out of Scope

The following generally do not qualify as security vulnerabilities without a demonstrated security impact:

- Ordinary syntax errors
- Expected compiler diagnostics
- Unsupported platforms
- Feature requests
- Documentation errors
- Performance issues without a security impact
- Compiler crashes without an exploitable security consequence
- Problems caused exclusively by unauthorized modifications

Reports are evaluated individually.

## Contact and Resources

**Official Repository:**  
https://github.com/vitte-lang/vitte

**Private Vulnerability Reporting:**  
https://github.com/vitte-lang/vitte/security/advisories/new

**Security Advisories:**  
https://github.com/vitte-lang/vitte/security/advisories

**Official Releases:**  
https://github.com/vitte-lang/vitte/releases

**Project Website:**  
https://vitte-lang.org

---

## Final Statement

The Vitte project is committed to improving compiler reliability, software security, and the integrity of its development and release processes.

Responsible vulnerability reporting, reproducible testing, and transparent security maintenance are important to the long-term development of the language.

**Vitte Programming Language — Security Policy**

Maintained by the Vitte Team.
