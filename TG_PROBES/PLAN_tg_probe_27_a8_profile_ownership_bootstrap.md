# PLAN_tg_probe_27_a8_profile_ownership_bootstrap
Parent: PLAN_tg_probe_14_core_extraction.md
Previous packet: PLAN_tg_probe_26_a7_3_hosted_console_mode_checkpoint.md
Policy: ../TG_CHANGE_POLICY.md
Status: [TODO] A8 implementation packet revised for safety. BLOCKED pending A8.0 ownership-identity migration probe outcome; no source implementation in this packet **high**

## Purpose
Define an implementation-safe A8 hosted-console profile bootstrap that demonstrates deterministic copied-profile classification only:
1. ready
2. passcode-required (or passcode-required-legacy)

This packet explicitly excludes interactive prompt/account chooser UX. That work remains in A9.

## Packet 26 Compatibility Lock
1. Keep packet-26 checkpoint behavior unchanged when `-console-profile` is absent.
2. Preserve untracked hosted demo tooling and do not require changes to `Telegram/tg_cli/tools/run_hosted_console_demo.ps1`.
3. Re-run packet-26 review harness in every A8 validation cycle.

## Roadmap Pointer Decision
1. A8 is not sole-ready while ownership identity migration risk is unresolved.
2. A8.0 ownership-identity migration probe is the sole ready next packet.
3. A9 remains closed.

## Locked Scope And Non-Goals
1. Hosted architecture only under `tg.exe -console`.
2. New opt-in switch for copied profile diagnostics: `-console-profile`.
3. Explicit `-workdir` is mandatory and canonicalized before ownership hash inputs.
4. Deterministic non-interactive outcomes only; no passcode prompt, no account chooser, no status UX expansion.
5. No writes by default to copied profile trees for missing/corrupt/passcode-required diagnostics.
6. No marker stamping in copied/real profile mode.
7. No fallback behavior that mutates profile state (`startFromScratch`, legacy auto-success fallback, `writeAccounts`) in diagnostics path.

## Review-Grounded Corrections Applied
1. A8 goal correction:
   - A8 demonstrates deterministic copied-profile classification only (`ready`, `passcode-required`, `passcode-required-legacy`).
   - A9 owns prompt/retry/account-selection/status UX.
2. Pre-ownership mutator prohibition for `-console-profile`:
   - before Sandbox ownership proof: no `Logs::start`, no `Local::start`, no default status sink under `<workdir>/tdata`, and no other confirmed profile-mutating writes.
   - require explicit `-console-log` path outside profile workdir; reject if path is inside canonical workdir tree.
3. Secondary-owner contract:
   - Sandbox secondary path emits `console-profile-status:busy-owner` and exits with code `20` before Application construction.
   - response wait/exit path remains bounded and fail-closed.
4. Storage diagnostics API contract:
   - add richer diagnostics path that cannot call `startFromScratch`, cannot apply legacy auto-success fallback, and cannot call `writeAccounts`.
   - preserve desktop/default startup behavior.
5. Application branch contract:
   - replace packet-26 console-ready early-return behavior for `-console-profile`.
   - emit `console-profile-status:ready` only after diagnostics Domain start returns ready.
6. Sentinel contract for write detection:
   - collect recursive tree hash+metadata sentinel before and after diagnostics for missing/corrupt/passcode-required copied profiles.
   - allowed writes are empty by default; any unavoidable read-side repair write must be explicitly documented before implementation.
7. Ownership identity mismatch resolution:
   - evaluate smallest source-compatible global explicit-workdir canonicalization shared by desktop and console before Sandbox server-name hash.
   - if desktop compatibility risk exists, split to A8.0 migration probe and keep A8 profile startup blocked.
8. Startup order/lifecycle split:
   - do not claim ownership semantics without an explicit ordered split and retained lifecycle object across preflight/startup/teardown.

## Baseline Constraints (Code-Grounded)
1. Current launcher order is `Logs::start` before `Sandbox::start`; this conflicts with `-console-profile` safety and requires a narrow split path.
2. Sandbox server identity uses `QDir(cWorkingDir()).absolutePath()`-derived hashing; packet-26 hosted guard canonicalization is currently hosted-path scoped.
3. `Storage::Domain::start()` currently includes mutation fallback behavior on modern failure; diagnostics mode must bypass these mutation paths.
4. Packet-26 hosted checkpoint mode and marker policy remain untouched for non-profile console path.

## Required Behavioral Contract

### 1. Entry Preconditions
1. `-console-profile` requires `-console` and explicit `-workdir`.
2. Implicit/default workdir selection is rejected.
3. Canonicalized explicit workdir identity must be computed before ownership hash and before startup phases that can mutate profile state.
4. `-console-log` is mandatory in profile mode and must be outside canonical workdir tree.

### 2. Ordered Startup Split And Lifecycle
1. Phase P0 (argument/policy): parse and validate `-console-profile`, explicit `-workdir`, explicit `-console-log` placement.
2. Phase P1 (ownership preflight in Sandbox): perform ownership handshake/listen decision and retain ownership object for process lifetime.
3. Phase P1 secondary outcome: emit busy-owner status and bounded exit code `20` before Application.
4. Phase P2 (primary only): start logs/local/startup components with explicit non-profile status sink.
5. Phase P3 (Application profile bootstrap): run diagnostics Domain start path.
6. Phase P4 (teardown): retain ownership until storage/domain teardown is complete, then release.

### 3. Deterministic Statuses And Exit Codes
Emit exactly one deterministic line prefix:
1. `console-profile-status:<status>`

Statuses:
1. ready
2. busy-owner
3. invalid-args
4. missing-workdir
5. invalid-workdir
6. non-canonical-workdir
7. invalid-console-log
8. profile-not-found
9. profile-corrupt
10. passcode-required
11. passcode-required-legacy
12. startup-blocked
13. internal-error

Exit codes:
1. `0` -> ready
2. `10` -> invalid-args
3. `11` -> missing-workdir
4. `12` -> invalid-workdir
5. `13` -> non-canonical-workdir
6. `14` -> invalid-console-log
7. `20` -> busy-owner
8. `30` -> profile-not-found
9. `31` -> profile-corrupt
10. `32` -> passcode-required
11. `33` -> passcode-required-legacy
12. `40` -> startup-blocked
13. `50` -> internal-error

### 4. Storage Diagnostics Taxonomy (No Mutation Path)
1. Additive diagnostics result taxonomy (exact symbols can vary, semantics fixed):
   - Ready
   - PasscodeRequired
   - PasscodeRequiredLegacy
   - ProfileNotFound
   - ProfileCorrupt
   - StartupBlocked
   - InternalError
2. Diagnostics path must never invoke:
   - `startFromScratch`
   - legacy auto-success fallback path
   - `writeAccounts`
3. Existing desktop startup path remains unchanged.

### 5. Sentinel Write-Invariance Contract
1. Compute pre-start recursive tree sentinel for copied profile test roots:
   - path list (relative)
   - file size
   - last-write timestamp
   - content hash (files)
2. Run diagnostics startup.
3. Compute post-start sentinel.
4. Required default: exact sentinel match for missing/corrupt/passcode-required outcomes.
5. Any mismatch is a stop condition unless prior plan text explicitly documents unavoidable read-side repair writes.

## A8.0 Ownership-Identity Migration Probe Gate
Status: [TODO] sole ready follow-up packet before A8 implementation **high**

Question:
1. Can explicit-workdir canonicalization be made global and source-compatible for both desktop and hosted console before Sandbox hash identity without breaking existing owner identity expectations?

Probe outcomes:
1. If YES:
   - A8 can proceed as sole implementor packet.
2. If NO or compatibility risk is non-trivial:
   - keep A8 blocked.
   - execute A8.0 migration packet and define compatibility strategy first.

Required A8.0 tests:
1. Desktop owner started with canonical path, second start via alias path -> busy-owner.
2. Desktop owner started with alias path, second start via canonical path -> busy-owner.
3. Hosted owner canonical + desktop alias mix -> single owner only.
4. Mixed-case/case-variant and junction/symlink alias paths -> same owner identity.
5. Existing explicit-workdir desktop startup regression remains stable.

## Protected Files And Expected Fences

### 1. core/launcher.cpp
Fence IDs:
1. launcher-console-profile-argument-parse
2. launcher-console-profile-flag-assign
3. launcher-console-profile-startup-phase-split
4. launcher-console-profile-log-path-policy

### 2. SourceFiles/settings.h
Fence IDs:
1. core-console-profile-launch-state
2. core-console-profile-log-policy-state

### 3. SourceFiles/settings.cpp
Fence IDs:
1. core-console-profile-launch-state
2. core-console-profile-log-policy-state

### 4. core/sandbox.cpp
Fence IDs:
1. sandbox-console-profile-ownership-preflight
2. sandbox-console-profile-secondary-status-code20
3. sandbox-console-profile-bounded-secondary-exit

### 5. core/application.cpp
Fence IDs:
1. application-console-profile-branch-replace-packet26-early-return
2. application-console-profile-domain-diagnostics-ready-only
3. application-console-profile-status-exit-map

### 6. storage/storage_domain.h
Fence ID:
1. storage-domain-profile-diagnostics-api

### 7. storage/storage_domain.cpp
Fence IDs:
1. storage-domain-profile-diagnostics-taxonomy
2. storage-domain-profile-diagnostics-no-start-from-scratch
3. storage-domain-profile-diagnostics-no-write-accounts

### 8. main/main_domain.cpp
Fence ID:
1. domain-console-profile-diagnostics-routing

### 9. core/logs.cpp (only if required by split)
Fence ID:
1. logs-console-profile-defer-preownership-mutators

### 10. TG-owned hosted helpers (no fences required)
1. Telegram/tg_cli/hosted/hosted_console_profile_guard.h
2. Telegram/tg_cli/hosted/hosted_console_profile_guard.cpp
3. Telegram/tg_cli/hosted/hosted_console_profile_diagnostics.h
4. Telegram/tg_cli/hosted/hosted_console_profile_diagnostics.cpp

## Ordered Edit Plan (Implementation Packet Guidance)
1. Implement launch-state additions for `-console-profile` and mandatory external `-console-log` policy.
2. Implement startup phase split so ownership preflight occurs before pre-ownership mutators.
3. Implement Sandbox secondary busy-owner status/code `20` bounded path pre-Application.
4. Add diagnostics-only Storage startup API with exact no-mutation guarantees.
5. Replace profile-mode packet-26 early return in Application and emit `ready` only after diagnostics Domain start success.
6. Add sentinel capture utility used only by tests/harness for before/after write-invariance checks.
7. Validate packet-26 regression unchanged.

## Validation Matrix (Required)

### 0. Static/build checks
1. Fence checker PASS against base.
2. `git diff --check` PASS.
3. `Telegram` Debug build PASS.
4. `tg_cli` build/help regression PASS.

### 1. Ownership/identity checks
1. Busy-owner same canonical path -> status busy-owner, code `20`, pre-Application.
2. Alias/junction/case-variant paths map to same owner identity.
3. Mixed desktop/console canonical-vs-alias runs remain single-owner only.

### 2. Diagnostics classification checks
1. Missing copied profile -> profile-not-found, no writes.
2. Corrupt copied profile -> profile-corrupt, no writes.
3. Passcode-required copied profile -> passcode-required or passcode-required-legacy, no writes.
4. Valid copied authenticated profile -> ready.

### 3. Regression checks
1. Non-console desktop startup unchanged.
2. Packet-26 checkpoint script unchanged:
   - `powershell -ExecutionPolicy Bypass -File Telegram/tg_cli/tools/test_hosted_console_checkpoint_packet26_review.ps1`

### 4. Sentinel checks
1. Capture and compare before/after recursive tree sentinel for each negative classification scenario.
2. Any unplanned write is failure.

## Validation Commands (Reference)
1. `cmake --build out --config Debug --target Telegram`
2. `cmake --build out --config Debug --target tg_cli`
3. `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo . --base <base_commit>`
4. `git diff --check`
5. `powershell -ExecutionPolicy Bypass -File Telegram/tg_cli/tools/test_hosted_console_checkpoint_packet26_review.ps1`

## Stop Conditions
1. Any pre-ownership profile mutator remains active in `-console-profile` mode.
2. `-console-log` cannot be enforced outside profile workdir tree.
3. Secondary busy-owner cannot be emitted pre-Application with bounded exit.
4. Diagnostics path still reaches `startFromScratch`, legacy auto-success fallback, or `writeAccounts`.
5. Sentinel mismatch indicates unplanned writes for missing/corrupt/passcode-required scenarios.
6. Packet-26 checkpoint behavior regresses.
7. Ownership identity migration risk remains unresolved; keep A8 blocked and continue A8.0.

## A9 Gate
A9 remains closed until:
1. A8.0 ownership identity decision is complete.
2. A8 diagnostics classification and zero-write matrix pass.
3. Desktop and packet-26 regressions pass.

## Planning-Only Note
This packet is planning-only. No A8 source implementation is authorized by this document.
