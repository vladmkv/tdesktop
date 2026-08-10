# PLAN_tg_probe_31_fence_quality_and_merge_rehearsal
Parent: PLAN_tg_probe_14_core_extraction.md
Previous packet: PLAN_tg_probe_30_a9_existing_account_enumeration.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Review corrections and fresh full merge/rebase rehearsals passed; user accepted and committed packet 31 as `8959a81420`. **high**

## Purpose
Deliver an implementation-ready, reusable pre-A10 maintenance toolchain that strengthens fence quality discipline and rehearses upstream merge/rebase safety without modifying the current branch state.

This packet began as planning-only and now records the implemented, validated, uncommitted acceptance candidate.

## Branch And Upstream Baseline (Grounded)
1. Current branch: `tg-cli`.
2. Remotes:
   - `origin` -> `git@github.com:vladmkv/tdesktop.git`
   - `upstream` -> `https://github.com/telegramdesktop/tdesktop.git`
3. Merge-base evidence captured for this packet:
   - `merge-base(HEAD, origin/dev) = 12e8d4a956b0e73c1f738112870a32ea079fa05f`
   - `merge-base(HEAD, origin/master) = 86262333a457f62709726ee4ab9c48fa58824da4`
4. Current working tree at packet authoring time: clean (`git status --short` empty).
5. Existing checker baseline at packet authoring time:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test` -> PASS
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo . --base 12e8d4a956` -> PASS

## Scope
1. Machine-readable TG fence manifest with fence-group relationships, ownership, intent, tests, and removal conditions.
2. Extend the existing checker (`check_tg_change_fences.py`) into a multi-mode toolchain; do not create a separate unrelated parser.
3. Add automated manifest inventory generator/update/check mode.
4. Add automated disposable git worktree merge/rebase rehearsal mode that never mutates the current branch.
5. Plan and execute fence-shrink prep, prioritizing broad fenced blocks (especially `storage-domain-owner-account-factory`).

## Explicit Non-Goals
1. No A10 feature implementation in this packet.
2. No changes to product behavior or runtime command contracts outside toolchain/inventory/reporting.
3. No edits to `lib_*` or vendored dependencies.
4. No direct mutation of the developer's currently checked-out branch during rehearsal.

## Current Broad-Fence Evidence (Grounded)
1. `Telegram/SourceFiles/storage/storage_domain.cpp` has `storage-domain-owner-account-factory` from line 150 to line 346 (broad cohesive region with mixed responsibilities).
2. This block currently includes at least:
   - legacy-account startup fallback branch,
   - account-added startup path,
   - snapshot classification passthrough,
   - account bootstrap/write helpers,
   - modern account-load loop using owner account factory,
   - scratch-start fallback.

## Design

### 1. Machine-Readable Manifest
1. Manifest file path: `TG_PROBES/tg_change_fence_manifest.json`.
2. Format: JSON only (Python stdlib friendly).
3. Top-level required fields:
   - `schemaVersion` (int)
   - `repoPolicy` (string, expected `TG_CHANGE_POLICY.md`)
   - `generatedAtUtc` (ISO-8601 Z)
   - `baseRevision` (40-char SHA)
   - `activePacket` (string)
   - `fences` (array)
   - `groups` (array)
4. Fence entry required fields:
   - `id` (string)
   - `file` (repo-relative path)
   - `beginLine` (int)
   - `endLine` (int)
   - `ownerPacket` (string, example `PLAN_tg_probe_22_a6_domain_lifecycle_account_factory.md`)
   - `intent` (short stable statement)
   - `tests` (array of command identifiers)
   - `removalCondition` (string)
   - `status` (`active|candidate-remove|exception-approved`)
   - `exceptionRationale` (string; required non-empty only for `exception-approved`)
   - `rationaleReviewedAtUtc` (ISO-8601 Z; required only for `exception-approved`)
   - `groupIds` (array)
   - `lastTouchedCommit` (SHA or empty)
5. Group entry required fields:
   - `groupId` (string)
   - `kind` (`header-cpp-pair|multi-file-feature|single-file-cluster`)
   - `members` (array of `file:id` strings)
   - `cohesionRationale` (string)
   - `splitPolicy` (`must-split|allowed-broad-exception`)

### 2. Checker Extension (Reuse Existing Parser)
1. Keep primary parser logic in `check_tg_change_fences.py` and extend it with modes/subcommands.
2. Required modes:
   - `validate` (existing behavior, default)
   - `quality`
   - `inventory`
   - `rehearsal`
   - `report` (render deterministic summary from prior JSON)
3. `quality` mode required checks:
   - fence block warning/failure thresholds (`--warn-fence-lines`, `--max-fence-lines`)
   - minimum changed-line density (`--warn-min-change-density`, `--min-change-density`); higher density is better
   - stale block warning/failure thresholds (`--warn-stale-days`, `--max-stale-days`) from fence-line commit age
   - global ID group consistency (manifest group/member validation)
   - base ancestry and current merge-base verification (`HEAD` ancestry against configured base)
   - exact upstream-file scope check (ensure only configured upstream-owned file patterns are audited)
4. JSON report output is required for every mode using `--json-report <path>`.
5. Human-readable output remains for CI/log visibility.

### 2.1 Legacy CLI Compatibility (Mandatory)
1. Existing no-mode invocation remains valid and maps to `validate`:
   - `python check_tg_change_fences.py --repo . --base <sha>`
2. Existing `--self-test` form remains valid without a mode.
3. Existing flag meanings and default `--base HEAD` remain unchanged for legacy invocation.
4. Removing or requiring a positional mode is forbidden in packet 31.
5. Integration tests must cover both legacy forms and new explicit `validate` form with equivalent results.

### 3. Inventory Generator / Update / Check
1. Implement as `inventory` mode in the same Python checker.
2. Required operations:
   - `inventory --generate` creates baseline manifest from current fence parse.
   - `inventory --update` refreshes line spans/commit metadata while preserving owner/intent/tests/removalCondition fields.
   - `inventory --check` verifies manifest fidelity against source files and fails on drift.
3. Drift categories:
   - missing fence in code,
   - unknown fence not in manifest,
   - moved lines,
   - group-member mismatch,
   - invalid owner packet reference,
   - stale `exception-approved` without rationale.

### 4. Disposable Worktree Merge/Rebase Rehearsal
1. Implement as `rehearsal` mode in the checker (or a thin helper module imported by it).
2. Invariants:
   - never checkout/rebase/merge on current branch workspace,
   - use a disposable worktree as a direct sibling of the repository (`<repo-parent>/.tg-rehearsal-<token>`), preserving Telegram's expected sibling dependency layout,
   - always cleanup unless `--keep-failure-worktree` is explicitly set.
3. Required rehearsal steps:
   - resolve target upstream base/ref,
   - optional fetch policy:
     - `--no-fetch` (default local-only)
     - `--fetch <remote> <refspec>` (explicit, opt-in)
   - verify new upstream base is reachable and merge-base is deterministic,
   - run merge or rebase rehearsal in disposable worktree,
   - run fence checks in rehearsal tree,
   - run configured build/test command sequence,
   - emit deterministic JSON report with phase-by-phase status,
   - cleanup and return status code.
4. No network access unless explicit `--fetch` is passed.
5. Worktree mode support:
   - `--mode merge`
   - `--mode rebase`
6. Build isolation:
   - configure a fresh build root at `<worktree>/out-rehearsal`; never reuse the primary worktree's `out`, object files, or generated outputs,
   - resolve and verify source/worktree/build paths before running commands; fail if build path is outside the disposable worktree,
   - run an explicit configure command before any build,
   - configure inputs come from committed config plus an allowlist imported from the primary `out/CMakeCache.txt`: `QT_DIR`, `TDESKTOP_API_ID`, `TDESKTOP_API_HASH`, generator/platform/toolset,
   - imported API values are passed directly to CMake but always redacted from human/JSON reports and command echoes,
   - if the primary cache is absent, required values must come from environment/config; missing values are `skipped-prerequisite` for build phases, never PASS.

### 4.1 Base Authority Matrix
1. Normal `validate`/`quality`/`inventory --check`:
   - manifest `baseRevision` is authoritative,
   - optional CLI `--base` must equal it, otherwise exit 4,
   - base must be a 40-character commit and ancestor of HEAD.
2. Rehearsal before git operation:
   - committed manifest base validates the source branch exactly as normal mode.
3. Rehearsal after merge/rebase:
   - resolved target SHA is the temporary validation base, so checks measure TG deltas against the upstream state being rehearsed rather than mixing target-side changes since the common ancestor,
   - manifest content is not rewritten,
   - report records both committed base and temporary rehearsal base,
   - inventory fidelity ignores `baseRevision` mismatch only in this explicit rehearsal context; all fence/member checks still apply.
4. Any other base mismatch fails exit 4.

### 4.2 Changed-Line Density Algorithm
1. Denominator: fence content lines only (`endLine - beginLine - 1`), excluding marker lines; minimum denominator 1.
2. Numerator: unique current-file line positions attributed to additions plus deletion anchors that overlap the block, using the existing zero-context hunk parser.
3. A replacement at one position counts once even when it has both deletion and addition.
4. Pure deletion anchors at `endLine + 1` count for the adjacent block only under the existing deletion-overlap rule.
5. Git renames use the destination path and `--find-renames`; unchanged moved text does not count as changed density.
6. Fence marker line movement alone contributes zero changed content lines.
7. Density is capped at 1.0.
8. Locked examples:
   - 10-line block with 4 added positions and 2 distinct deletion-only anchors: density `6/10 = 0.60`.
   - 100-line wrapper with 3 replacement positions (each add+delete): density `3/100 = 0.03`, hard failure.

### 4.3 Deterministic Report Paths
1. Committed reports contain repository-relative paths only.
2. Repository root is rendered as `${REPO}`; disposable worktree as `${WORKTREE}`; build root as `${BUILD}`.
3. Machine-specific absolute paths, usernames, drive letters, and raw sensitive CMake values are forbidden in committed reports.
4. Runtime temporary paths may appear only in non-committed diagnostic logs.

### 5. Broad-Fence Shrink Strategy
1. Primary target: `storage-domain-owner-account-factory` in `Telegram/SourceFiles/storage/storage_domain.cpp`.
2. Required split classification output:
   - Cohesive exception candidates (can remain broad with explicit rationale),
   - Must-split regions (independent concerns that should be fenced separately).
3. Initial must-split candidates for this block:
   - legacy account start fallback branch in `Domain::start`,
   - modern account creation loop in `startModern`,
   - scratch-start path in `startFromScratch`.
4. Initial cohesive exception candidates (pending verification):
   - tightly coupled read/write helper group where split would reduce audit clarity.
5. Shrink work must preserve semantics exactly; no behavior changes.

## Planned File Changes (Exact)

### Existing files to modify
1. `Telegram/tg_cli/tools/check_tg_change_fences.py`
2. `PLAN_tg_console_mode.md` (canonical queue pointer only)
3. `TG_PROBES/PLAN_tg_probe_14_core_extraction.md` (roadmap pointer only)

### New TG-owned files to add
1. `TG_PROBES/tg_change_fence_manifest.json`
2. `TG_PROBES/tg_change_rehearsal_config.json`
3. `Telegram/tg_cli/tools/test_tg_change_fence_quality_packet31.ps1`

### Rehearsal Config Schema (Locked)
`TG_PROBES/tg_change_rehearsal_config.json` required fields:
1. `schemaVersion`: `1`.
2. `defaultTarget`: `upstream/dev`.
3. `worktreeNamePrefix`: `.tg-rehearsal-`.
4. `buildDirectory`: `out-rehearsal`.
5. `primaryBuildDirectory`: `out` (read-only input for allowlisted cache import; never used for rehearsal outputs).
6. `cacheImportKeys`: exactly `QT_DIR`, `TDESKTOP_API_ID`, `TDESKTOP_API_HASH`, `CMAKE_GENERATOR`, `CMAKE_GENERATOR_PLATFORM`, `CMAKE_GENERATOR_TOOLSET`.
7. `sensitiveKeys`: exactly `TDESKTOP_API_ID`, `TDESKTOP_API_HASH`; values are never written or echoed.
8. `configureCommand`: token array using placeholders, equivalent on Windows to `cmake -S {worktree} -B {build} -G {generator} -A {platform} -T {toolset}` plus imported `-D` values.
9. `commands`: ordered named command objects for fence validate, quality, inventory check, tg_cli build/help, Telegram build, packet-26 test, packet-30 test.
10. `optionalPrerequisites`: packet-30 requires `{repo_parent}/tg-dev-profile`; absence reports `skipped-prerequisite`.
11. Allowed placeholders: `{repo}`, `{repo_parent}`, `{worktree}`, `{build}`, `{python}`, `{configuration}`, `{generator}`, `{platform}`, `{toolset}`. Unknown placeholders fail exit 2.
12. Commands execute without shell interpolation (`subprocess.run` token arrays); no arbitrary command strings.

### Optional file (only if needed)
1. `Telegram/tg_cli/tools/run_tg_change_rehearsal.ps1` (thin wrapper to call Python mode; add only if command ergonomics materially improve repeatability)

## CLI Contract

### Checker base command
`python Telegram/tg_cli/tools/check_tg_change_fences.py <mode> [options]`

### Required commands
1. Validate fences:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py validate --repo . --base 12e8d4a956 --json-report TG_PROBES/reports/fence_validate.json`
2. Quality audit:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py quality --repo . --base 12e8d4a956 --manifest TG_PROBES/tg_change_fence_manifest.json --warn-fence-lines 40 --max-fence-lines 80 --warn-min-change-density 0.50 --min-change-density 0.20 --warn-stale-days 180 --max-stale-days 365 --json-report TG_PROBES/reports/fence_quality.json`
3. Inventory generate:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py inventory --repo . --base 12e8d4a956 --manifest TG_PROBES/tg_change_fence_manifest.json --generate --json-report TG_PROBES/reports/fence_inventory_generate.json`
4. Inventory update:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py inventory --repo . --base 12e8d4a956 --manifest TG_PROBES/tg_change_fence_manifest.json --update --json-report TG_PROBES/reports/fence_inventory_update.json`
5. Inventory check:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py inventory --repo . --base 12e8d4a956 --manifest TG_PROBES/tg_change_fence_manifest.json --check --json-report TG_PROBES/reports/fence_inventory_check.json`
6. Rehearsal local mode (no fetch):
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py rehearsal --repo . --manifest TG_PROBES/tg_change_fence_manifest.json --config TG_PROBES/tg_change_rehearsal_config.json --mode merge --target upstream/dev --no-fetch --json-report TG_PROBES/reports/rehearsal_local_merge.json`
7. Rehearsal local rebase mode (no fetch):
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py rehearsal --repo . --manifest TG_PROBES/tg_change_fence_manifest.json --config TG_PROBES/tg_change_rehearsal_config.json --mode rebase --target upstream/dev --no-fetch --json-report TG_PROBES/reports/rehearsal_local_rebase.json`
8. Explicit-fetch parser negative (no network):
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py rehearsal --repo . --manifest TG_PROBES/tg_change_fence_manifest.json --config TG_PROBES/tg_change_rehearsal_config.json --mode rebase --target upstream/dev --no-fetch --fetch upstream dev --json-report TG_PROBES/reports/rehearsal_fetch_rebase_parser_negative.json`

## Exit Codes
1. `0` success, no violations.
2. `1` fence validation violation(s).
3. `2` manifest/schema/input argument error.
4. `3` quality threshold failure (size/density/stale/group scope).
5. `4` ancestry or merge-base mismatch.
6. `5` inventory drift detected.
7. `6` rehearsal git operation failure.
8. `7` rehearsal build/test command failure.
9. `8` cleanup failure after rehearsal.
10. `9` internal unhandled tool error.

## Test Plan
1. Unit/self-test extension in checker:
   - manifest parse validation,
   - group matching,
   - stale/density thresholds,
   - ancestry/merge-base failure injections,
   - deterministic JSON schema validation.
2. Scripted integration test (`test_tg_change_fence_quality_packet31.ps1`):
   - run `validate`, `quality`, `inventory --check`,
   - run rehearsal merge with `--no-fetch` against local refs,
   - assert current branch/worktree hash unchanged before/after.
3. Negative tests:
   - malformed manifest,
   - unknown fence id,
   - stale exception without rationale,
   - rebase rehearsal conflict path,
   - configured test command failure.

## Locked Defaults And Decisions
1. Fence size: warn above 40 content lines; fail above 80. `exception-approved` may exceed 80 only with owner packet, cohesion rationale, tests, and removal condition. No exception may exceed 120 lines.
2. Changed-line density (changed lines attributed to the fence divided by fenced content lines): warn below 0.50; fail below 0.20. Density failure cannot be waived because wrapping unchanged upstream code is the primary merge risk.
3. Staleness uses the newest commit touching lines inside the specific fence: warn after 180 days; fail after 365 days unless an approved exception has a current rationale; `candidate-remove` fails after 30 days without disposition.
4. IDs are globally unique by default. Repeated header/cpp or cross-file IDs are legal only when every occurrence is declared in one manifest group.
5. Generated inventory entries missing owner, intent, tests, or removal condition fail `inventory --check` until enriched.
6. `baseRevision` must be a 40-character commit, an ancestor of HEAD, and equal the configured TG upstream base for normal validation. Rehearsal computes its new merge base without rewriting the committed manifest.
7. Default rehearsal target is local `upstream/dev` with `--no-fetch` (confirmed present). Network fetch remains explicit opt-in. Both merge and rebase modes must work only in disposable worktrees.
8. Minimum rehearsal profile, in order: fence validate/quality/inventory check; tg_cli build/help; stable single-compiler Telegram build (`MSBuild /m:1`, `_CL_=/MP1` on Windows); packet-26 and packet-30 tests when the persistent dev profile exists, otherwise explicit `skipped-prerequisite`.
9. Current broad-fence disposition:
   - `storage-domain-owner-account-factory` (195 lines): must split; no exception.
   - `storage-domain-snapshot-storage-read-classification` (90 lines): cohesive classifier; approved exception only with tests and removal condition.
   - `launcher-console-profile-snapshot-gates` (89 lines): must split by owner-probe/profile-status concern.
   - `launcher-console-accounts-gates` (53 lines): warning only; retain only if density is at least 0.50 and manifest metadata is complete.
   - `dav1d-github-mirror` (64 lines): warning only; split if density is below 0.20.
10. JSON keys and violation ordering are deterministic. Timestamps are metadata excluded from golden comparisons. Only final accepted reports are committed.

## Definition Of Done
1. Existing checker is extended (not replaced) and supports `validate`, `quality`, `inventory`, and `rehearsal` modes.
2. Machine-readable manifest exists and passes `inventory --check`.
3. Group relationships (header/cpp pairs and cross-file clusters) are represented and validated.
4. Quality mode enforces the locked warning/failure size thresholds, minimum changed-line density, stale blocks, group consistency, base ancestry, merge-base, and upstream-file scope.
5. Rehearsal mode performs disposable merge/rebase tests without changing current branch/worktree content.
6. Local no-fetch merge and local no-fetch rebase rehearsal modes both execute and produce deterministic JSON reports; explicit-fetch is validated as a parser/input-policy negative test without network access.
7. Broad-fence shrink plan is executed for at least one high-risk fence (`storage-domain-owner-account-factory`) with semantic-equivalent splitting and rationale for any retained broad exception.
8. A reproducible acceptance report is produced and attached to this packet.
9. No commit is made until user acceptance is explicitly recorded.
10. A10 remains blocked until this packet is accepted.

## Stop Conditions
1. Checker reuse becomes impractical and requires a second parser implementation.
2. Rehearsal cannot guarantee zero mutation of current branch/worktree.
3. Quality thresholds produce high false-positive rates that cannot be tuned with bounded configuration.
4. Broad-fence splitting requires semantic behavior change.
5. Required validation depends on non-stdlib Python packages.

If any stop condition is hit:
1. stop implementation,
2. capture exact failing evidence in packet devlog,
3. propose bounded follow-up packet.

## Rollback Plan
1. Revert packet-31 implementation commits only (toolchain files and manifest/config files).
2. Re-run baseline checker:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo . --base 12e8d4a956`
3. Confirm branch returns to pre-packet behavior with no fence-regression side effects.

## Acceptance Report Contract
Create one report file per run family under `TG_PROBES/reports/` and one summary markdown:
1. `TG_PROBES/reports/fence_validate.json`
2. `TG_PROBES/reports/fence_quality.json`
3. `TG_PROBES/reports/fence_inventory_check.json`
4. `TG_PROBES/reports/rehearsal_local_merge.json`
5. `TG_PROBES/reports/rehearsal_local_rebase.json`
6. `TG_PROBES/reports/rehearsal_fetch_rebase_parser_negative.json`
7. `TG_PROBES/NOTE_tg_probe_31_acceptance_report.md`

Summary markdown must include:
1. exact command lines,
2. exit codes,
3. violation counts by class,
4. rehearsal phase timeline,
5. current-branch integrity proof (`git rev-parse HEAD` and `git status --short` pre/post),
6. unresolved decisions and disposition.

## Ordered Implementation Tasks
1. [DONE] Add checker mode scaffolding and shared JSON report writer. **high**
   Outcome: Extended `check_tg_change_fences.py` with `validate|quality|inventory|rehearsal|report` mode dispatch, deterministic JSON writer, and verified legacy/no-mode + explicit `validate` + `--self-test` all PASS against base `12e8d4a956`.
2. [DONE] Implement manifest schema and inventory generate/update/check mode in the existing checker. **high**
   Outcome: Added locked schema validation, deterministic manifest read/write, inventory `--generate|--update|--check`, drift classes, owner packet validation, and deterministic auto-group reconciliation; `inventory --check` now reports `driftCount=0`.
3. [DONE] Implement quality mode thresholds and ancestry/merge-base/upstream-file-scope checks. **high**
   Outcome: Added locked thresholds for size/density/staleness, global group-ID consistency, base authority verification, deterministic JSON warnings/violations, and rehearsal temporary-base support; branch quality report exits `0` with warnings only.
4. [DONE] Implement disposable worktree rehearsal mode (`merge`/`rebase`) with deterministic cleanup/reporting. **high**
   Outcome: Added sibling disposable worktree orchestration, merge/rebase mode, no-fetch default, optional explicit fetch path, in-worktree manifest/config sync, isolated `${BUILD}` root, sensitive value redaction, and deterministic phase JSON. Local no-fetch merge/rebase executed end-to-end and both stopped at quality gate with exit `3` (captured in reports).
5. [DONE] Add packet-31 integration test script and deterministic report validation. **medium**
   Outcome: Added `Telegram/tg_cli/tools/test_tg_change_fence_quality_packet31.ps1`; parse check passed, and script run (`-SkipRehearsal`) produced `PACKET31_TEST=PASS` with validate/quality/inventory report outputs and branch-integrity assertion.
6. [DONE] Execute broad-fence split pass for `storage-domain-owner-account-factory` with no semantic changes and classify retained exceptions. **medium**
   Outcome: Split broad owner-account factory fence into semantic narrow blocks (`legacy-alloc`, `modern-loop`, `scratch`) and narrowed related broad wrappers (`launcher` gate split, `dav1d` mirror line-only fence, density-driven mini-splits in `main_account.cpp`/`main_domain.cpp`); retained `storage-domain-snapshot-storage-read-classification` as `exception-approved` with rationale+timestamp in manifest.
7. [DONE] Re-run full packet acceptance matrix until rehearsal nonzero blockers are resolved; wait for explicit user acceptance before any commit. **high**
   Outcome: Validate, quality, and inventory pass; local no-fetch merge and native rebase rehearsals both exit `0`; isolated `tg_cli` and full Telegram builds plus packet-26/30 pass in both; explicit-fetch parser negative exits `2` without network; full integration passes by reusing the fresh reports.
8. [DONE] In rehearsal mode, build an in-memory refreshed manifest overlay keyed by `file:id` from the post-merge/post-rebase worktree, update spans only, preserve metadata fields, and run quality/inventory semantic checks against overlay spans while recording moved-line counts as informational metadata. **high**
   Outcome: Implemented overlay refresh (`manifest-overlay-refresh` phase) with preserved metadata and span-only updates; moved-fence count is now informational metadata, while rehearsal quality/inventory check against overlay spans passes for shifted `tg-executable-name` (`density=1.0`, drift `0`).
9. [DONE] Add local/no-fetch submodule initialization and required-submodule verification in disposable worktree before configure (`git submodule update --init --recursive --no-fetch`), with precise skipped-prerequisite reporting when required objects are unavailable. **high**
   Outcome: Added `submodule-init` and `submodule-verify` rehearsal phases; required set includes `cmake` and all CMake-consumed Telegram lib/codegen submodules. Configure now runs inside `${WORKTREE}`/`${BUILD}` after local no-fetch submodule init.
10. [DONE] Extend packet-31 regression coverage for shifted fence spans, synthetic snapshot integrity, no temp refs/worktrees leakage, report sanitization (absolute path and sensitive value absence), zero-SHA handling, and legacy CLI syntax parity. **high**
   Outcome: Expanded `test_tg_change_fence_quality_packet31.ps1` with legacy validate checks, zero-SHA base rejection check, zero `lastTouchedCommit` normalization probe, synthetic snapshot probe validation, temp ref/worktree assertions, absolute-path/sensitive-value report scans, and packet26/30 integration invocations.
11. [DONE] Make disposable worktree cleanup verify both Git deregistration and directory removal, with a standard-library Windows long-path fallback after `git worktree remove` fails. **high**
   Description: The corrected merge rehearsal passed configure, both builds, help, packet-26, and packet-30, but exited `8` because packet-26 left its alias-owner process alive: its finalizer compared a canonical process path with a mixed-separator configured binary path. Git then deregistered the worktree while leaving `.tg-rehearsal-5eedf4707d` on disk with locked binaries and `Invalid argument`.
   Definition of Done: packet-26 canonicalizes its binary path and leaves no process for that binary; focused cleanup self-test passes; cleanup reports success only when the worktree is absent from Git metadata and the directory no longer exists; a real merge rehearsal exits `0` without process or sibling worktree leaks.
   Outcome: Canonicalized packet-26's binary path, added long-path/read-only cleanup fallback plus Git/filesystem postcondition checks, verified zero surviving process, and completed local no-fetch merge rehearsal with exit `0` and no disposable worktree leak.
12. [DONE] Correct packet-31 integration report reuse so the synthetic snapshot probe is required only when the integration run creates a new merge report. **high**
   Description: Reuse currently creates a new untracked probe after selecting an existing report, then incorrectly expects the older synthetic commit to contain that new probe.
   Definition of Done: fresh-run mode still proves untracked current-worktree snapshot capture; reuse mode validates the green report without an impossible post-report probe assertion; full integration exits `0` while reusing both green reports.
   Outcome: Probe creation/assertion now runs only for newly generated merge reports; the complete integration reused both accepted reports and exited `0` while retaining fresh-run snapshot coverage.
13. [TODO] Bind rehearsal report reuse to the exact relevant worktree content and current target SHA, not only HEAD and manifest/config timestamps. **high**
   Description: Independent review showed uncommitted checker/source changes or an advanced `upstream/dev` could leave current reuse checks unchanged.
   Definition of Done: reports carry deterministic input and target fingerprints; reuse rejects any relevant content or target change; positive and negative integration tests pass.
14. [TODO] Guarantee local submodule materialization cannot contact the network under `--no-fetch`. **high**
   Description: `git submodule update --no-fetch` can still clone an absent module using configured remote URLs.
   Definition of Done: rehearsal maps required submodules to verified local primary-worktree repositories and fails as skipped prerequisite when local objects are unavailable; no remote URL is used.
15. [TODO] Return documented input failure/report behavior for invalid configured command placeholders. **medium**
   Description: `resolve_command_tokens()` raises `ValueError`, while the rehearsal execution handler currently catches only `RuntimeError`.
   Definition of Done: unknown placeholders exit `2`, produce deterministic JSON when requested, clean temporary state, and have a negative test.
16. [TODO] Exclude marker-only edits from changed-line density. **medium**
   Description: deletion-anchor clamping can map marker-adjacent edits into content lines.
   Definition of Done: marker-only begin/end edits contribute zero density numerator; genuine fenced content edits remain counted; self-tests pass.
17. [TODO] Harden report path sanitization for replacement order and paths containing spaces. **medium**
   Description: parent-first replacement emits `${REPO}/../tdesktop`, and whitespace-bounded absolute-path matching can preserve path suffixes.
   Definition of Done: longest/specific roots redact first; Windows absolute paths with spaces are fully tokenized; reports contain no local path fragments; tests cover both cases.
18. [TODO] Remove the leaked random snapshot probe and add cleanup/fault coverage for partial worktree creation. **low**
   Description: `packet31_snapshot_probe.txt` remained after report reuse, and cleanup is conditional on the worktree-add success flag.
   Definition of Done: no probe artifact remains; cleanup examines actual registration/directory postconditions even after partial add; focused tests pass.
19. [TODO] Keep local submodule URL overrides command-scoped and initialize only the verified required paths. **high**
   Description: Independent re-review identified that linked worktrees share `.git/config`, so `git config submodule.*.url` would mutate primary state; unrestricted recursive update would also attempt unmapped HTTPS submodules under file-only protocol.
   Definition of Done: no persistent Git config writes occur; one command uses `-c submodule.<name>.url=<primary-path>` for each required path, updates only those paths without recursion, and full configure/build rehearsal passes with file-only protocol.
20. [TODO] Include initialized submodule worktree HEADs in rehearsal input fingerprints. **high**
   Description: Hashing only staged gitlink SHAs misses a locally checked-out submodule commit that `git add -A` captures in the synthetic snapshot.
   Definition of Done: changing a submodule HEAD changes the fingerprint representation; normal gitlinks remain deterministic; focused tests pass.
21. [TODO] Sanitize early rehearsal input errors before JSON output and make keep-failure branch retention exact. **medium**
   Description: Manifest/config parser errors bypass normal report sanitization, and failed rebase add retries can retain/report branch state imprecisely.
   Definition of Done: early error summaries redact absolute paths; only branches that actually exist are retained and all retained refs are reported; tests pass.

## Unresolved Decisions
1. None. Defaults, exception rules, target policy, and validation profile are locked above. Implementation must stop rather than invent new policy.

## Notes
1. All paths in this packet are repository-relative and clone-portable.
2. Python standard library is required; third-party Python dependencies are forbidden.
3. PowerShell wrapper scripts are optional and should remain thin pass-through wrappers only if needed.