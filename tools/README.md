# Repository Tools

PowerShell automation is grouped by responsibility:

- `bootstrap/`: download and validate development/runtime dependencies.
- `ci/`: wrappers used by continuous integration jobs.
- `quality/`: repository policy, resource-limit, and coverage checks.
- `testing/`: fixture generation and hardware/performance gates.
- `release/`: release identity, certificate, and signing operations.

Run scripts from the repository root using their full path. Component-local compiled utilities,
such as the HLSL header compiler, remain beside their owning component under `src/`.
