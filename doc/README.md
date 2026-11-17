# doc/ — what lives where

Users start with the [user guide](user-guide/index.html). Contributors start
with the [developer guide](developer-guide/index.html). Everything else in this
folder is filed by kind:

| Location | What belongs there |
|----------|--------------------|
| [`design/`](design/) | Design documents and implementation plans, one per feature or task. `EMULATOR-DESIGN-PLAN.md` and the `TASK*` plans are frozen historical records; pending work lives in GitHub issues. `debug-subsystem/` holds the per-frontend designs of the debugger. |
| [`analysis/`](analysis/) | Analyses, audits and assessments: findings about the system or its references, not designs for it. |
| [`testing/`](testing/) | Test plans (`*-TEST-PLAN-DESIGN.md`, read by the traceability generator), the generated `TRACEABILITY-MATRIX.md`, the regression-suite docs and verification records. `audits/` holds closed test-audit records. |
| [`issues/`](issues/) | Investigation records of specific bugs and demos: logs, evidence and screenshots. Historical; cited from code comments. |
| [`obsolete/`](obsolete/) | Superseded or abandoned documents, kept for history and not maintained. See its README. |
| [`formats/`](formats/) | Generated file-format artifacts (the `.jns` snapshot schema). |
| [`perf/`](perf/) | Raw profiling data behind the Task 27 reports in `design/`. |
| [`legal/`](legal/) | The licensing analysis made before the first publication. |
| [`man/`](man/) | The man page: `jnext.1.md` is the source; `jnext.1` and the repository's `USAGE.md` are generated from it (`make docs-man`). |
| [`user-guide/`](user-guide/), [`developer-guide/`](developer-guide/) | Rendered sites, GENERATED from `src/doc/user-guide` and `src/doc/developer-guide`. Edit the sources, never these. |

Directly in this folder:

| File | What it is |
|------|------------|
| [`PULL-REQUEST-PROTOCOL.md`](PULL-REQUEST-PROTOCOL.md) | How pull requests are reviewed and merged. |
| [`RELEASE-PROTOCOL.md`](RELEASE-PROTOCOL.md) | Versioning, tagging, packaging and announcing releases. |
| [`REFERENCES.md`](REFERENCES.md) | Every external URL the project relies on. |
| [`DEVELOPMENT-SESSIONS.md`](DEVELOPMENT-SESSIONS.md) | Log of development sessions and time spent. |
| [`LINUX-BUILD-DOCKER.md`](LINUX-BUILD-DOCKER.md), [`WINDOWS-PORTING.md`](WINDOWS-PORTING.md) | Build notes for a Docker container and for the Windows cross-build; `BUILD.md` at the repository root is the main build guide. |
| `SCREENSHOT.png`, `JNEXT-NEXTZXOS-BOOT.png` | Screenshots used by `README.md` and the AppStream metadata; keep their paths. |
