# PLAN_tg_probe_28_a8_0_ownership_identity_migration
Parent: PLAN_tg_probe_14_core_extraction.md
Previous packet: PLAN_tg_probe_27_a8_profile_ownership_bootstrap.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] A8.0 bounded ownership-identity migration probe executed with max 3 attempts (A->B->C). Result: FAIL. A8 and A9 remain blocked **high**

## Purpose
Decide a source-compatible ownership identity migration strategy for explicit -workdir single-instance behavior before any A8 profile diagnostics work.

This packet is probe-only and migration-decision-only:
1. No profile open.
2. No Storage or Domain start.
3. Disposable directories only.
4. No lib_* edits.

## Inputs Read For This Packet
1. Packet 27 safety gate and A8.0 requirement: PLAN_tg_probe_27_a8_profile_ownership_bootstrap.md.
2. Packet 26 guard/runtime behavior: PLAN_tg_probe_26_a7_3_hosted_console_mode_checkpoint.md.
3. Probe 03 ownership conclusions: PLAN_tg_probe_03_profile_ownership.md and NOTE_tg_probe_results.md.
4. Current ownership code path:
   - Launcher explicit workdir parse and hosted canonical rewrite.
   - Sandbox local-server identity hash on QDir(cWorkingDir()).absolutePath().
   - Windows global local server naming and existing second-instance protocol.

## Current Protocol Baseline (Code-Grounded)
1. Primary single-instance identity is local-server name derived from MD5 of QDir(cWorkingDir()).absolutePath(), then wrapped by Platform::SingleInstanceLocalServerName(hash).
2. Explicit -workdir currently becomes absolute with trailing slash in Launcher argument processing.
3. Hosted checkpoint guard canonicalizes explicit workdir and may rewrite Launcher custom workdir to canonical path before Logs/Sandbox startup.
4. Secondary instance protocol is socket connect, command send (CMD:show or CMD:console), wait for RES:<pid>_<windowId>; then secondary exits.
5. Existing executable-path QLockFile is not the profile ownership primitive for custom workdirs; Probe 03 marked alias-based identity split as a risk.

## Locked Scope
1. Probe only identity/listen/connect behavior.
2. Do not open Domain or Storage in probe runs.
3. Do not access real profile paths.
4. Use only disposable test roots and disposable copied binaries.
5. Keep packet-26 hosted checkpoint safety behavior unchanged during probing.

## Non-Goals
1. No A8 profile status pipeline implementation.
2. No A9 prompt/account/status UX.
3. No storage lock format migration in this packet.

## Options Under Comparison

### A) Global canonical explicit-workdir identity replacement
Definition:
1. Replace ownership hash input for explicit workdir with canonical path identity globally (desktop + hosted), instead of absolute path text.

What it may fix:
1. Canonical/alias/junction/case variants converge to one identity when canonicalization succeeds.

Risks:
1. Compatibility split with already-running/older binaries still hashing absolute-path spelling.
2. Canonicalization failure modes can become startup blockers where absolute path previously worked.

Required compatibility probe focus:
1. Old baseline binary owner + new candidate secondary.
2. New candidate owner + old baseline secondary.
3. Mixed alias spelling and canonical spelling across both binaries.

### B) Dual-server migration identity (legacy absolute hash + canonical hash)
Definition:
1. During migration window, each process can listen/connect against both identities.
2. Deterministic primary arbitration selects one identity owner and prevents dual-primary deadlock.

What it may fix:
1. Backward compatibility with old binaries while converging aliases to canonical identity.

Risks:
1. Deadlock if two simultaneous launches each bind one identity then wait on the other.
2. Split-brain if arbitration order is not deterministic.
3. IPC complexity increase in startup critical path.

Required migration rules to prove:
1. Deterministic primary order (same algorithm for all processes).
2. No cycle wait; bounded fallback and fail-closed exit.
3. Same behavior for desktop/hosted command paths.

### C) Strict A8 restriction + independent filesystem lock in profile root
Definition:
1. Require exact canonical explicit path (no alias accepted) for A8 mode.
2. Add independent filesystem lock under profile root before any reads.
3. Still attempt to detect desktop owner started via alias.

Feasibility assessment requirement:
1. Determine whether alias-started desktop owner can actually be detected without changing desktop identity behavior.

Preliminary assessment:
1. C can reject non-canonical launch spelling for new mode.
2. C cannot reliably detect an already-running desktop owner started through an alias if desktop owner identity remains legacy absolute-hash spelling and lock does not participate in desktop ownership today.
3. Therefore C alone does not satisfy cross-spelling owner detection unless paired with additional migration behavior equivalent to A or B.

## Mixed-Version Baseline Rule (Mandatory)
1. Before any source changes, copy currently committed out/Debug/tg.exe to a disposable old-baseline binary path.
2. Treat that copy as old version in matrix execution.
3. Candidate binary for each attempt is the current build output after that attempt.
4. Never use real profile dirs; all workdirs are disposable.

## Exact Probe Matrix (Must Execute)

### Path Spellings
Prepare one disposable root and these workdir spellings that resolve to same target:
1. Canonical spelling.
2. Junction/symlink alias spelling.
3. Case-variant spelling.
4. Same-spelling control (exact identical text).

### Version Pairs
1. old -> old (control)
2. old -> new
3. new -> old
4. new -> new

### Start Orders
1. Sequential owner-first then secondary.
2. Simultaneous race launch (near-concurrent starts).

### Matrix Cells Per Version Pair
1. canonical owner, canonical secondary (same spelling control).
2. canonical owner, alias secondary.
3. alias owner, canonical secondary.
4. case-variant owner, canonical secondary.
5. canonical owner, case-variant secondary.
6. simultaneous canonical vs alias.

### Required Assertions Per Cell
1. Exactly one owner/listener process remains.
2. Secondary resolves via current single-instance protocol (RES response path) or deterministic fail-closed status.
3. No deadlock/hang beyond bounded timeout.
4. No tdata mutation beyond packet-26 disposable marker/log expectations.
5. No Domain/Storage initialization occurs.

## Instrumentation And Evidence
1. Capture process PIDs, listen/connect outcome, and command/response path per launch.
2. Capture local-server identity strings used by each process (legacy/canonical/both, as applicable).
3. Capture bounded timeout outcomes for race cells.
4. Keep per-cell artifacts under disposable output root only.

## Proposed Attempt Order (Max 3)
1. Attempt 1: Option A minimal global canonicalization prototype for explicit -workdir identity.
   - Goal: verify if compatibility risk is acceptably low in mixed-version matrix.
2. Attempt 2: Option B dual-identity listen/connect with deterministic primary arbitration and bounded no-deadlock protocol.
   - Trigger: Attempt 1 fails compatibility cells or introduces unacceptable regression risk.
3. Attempt 3: Option C strict canonical A8 restriction + independent filesystem lock pre-read, with explicit proof/disproof of alias-owner detection.
   - Trigger: Attempt 2 still fails to produce deterministic safe migration.

No fourth attempt is allowed in this packet line.

## Explicit Stop Decision
Stop and keep A8/A9 blocked if any of the following remain true after Attempt 3:
1. Any mixed-version alias/canonical matrix cell permits dual owners.
2. Any deterministic arbitration path can deadlock or hang unboundedly.
3. Alias-started desktop owner cannot be detected under the chosen design while claiming it can.
4. Design requires profile open, Storage/Domain start, or non-disposable path testing.

If stop condition triggers:
1. Record FAIL for A8.0.
2. Keep packet 27 (A8) blocked.
3. Keep A9 blocked.
4. Escalate to new architecture decision packet before implementation resumes.

## Protected Files Expected If Implementation Follows
1. Telegram/SourceFiles/core/launcher.cpp
2. Telegram/SourceFiles/core/sandbox.cpp
3. Telegram/SourceFiles/core/launcher.h (only if API exposure changes)
4. Telegram/SourceFiles/settings.h / settings.cpp (only if launch-state additions are strictly required)
5. Telegram/tg_cli/tools/* (probe harness scripts only)

No lib_* file changes allowed.

## Validation Commands (Reference)
1. cmake --build out --config Debug --target Telegram
2. powershell -ExecutionPolicy Bypass -File Telegram/tg_cli/tools/<packet28_probe_harness>.ps1
3. python Telegram/tg_cli/tools/check_tg_change_fences.py --repo . --base <base_commit>
4. git diff --check

## Packet Outcome Contract
1. Deliver one selected migration design (A, B, or C-with-proof-of-limitations) grounded in executed matrix evidence.
2. Declare A8.0 PASS only if all required matrix assertions pass and stop conditions are not hit.
3. Otherwise declare A8.0 FAIL and keep A8/A9 blocked.

## Execution Evidence (2026-07-31)
1. Baseline binary copy was created before source edits:
   - Old baseline path: `C:/Users/wd985049/AppData/Local/Temp/tg_probe28_29e501943ec34ce7a04c08fe8bc5eb1d/tg_old_baseline.exe`
   - Old baseline SHA256: `D3BBB3B572ED048BC030BBE7D819CA2DE0755E7BCBCB9E0AD3EC99B268632426`
2. Probe harness used for all attempts:
   - `Telegram/tg_cli/tools/test_packet28_ownership_identity_migration.ps1`
3. Attempt A (Option A prototype, global canonical explicit-workdir identity):
   - Build: PASS (`cmake --build out --config Debug --target Telegram`)
   - Matrix result: FAIL
   - Result JSON: `C:/Users/wd985049/AppData/Local/Temp/tg_probe28_29e501943ec34ce7a04c08fe8bc5eb1d/A/packet28_matrix_results.json`
   - Failed assertions summary: 24/24
4. Attempt B (Option B prototype, dual identity connect/listen fallback):
   - Build: PASS
   - Matrix result: FAIL
   - Result JSON: `C:/Users/wd985049/AppData/Local/Temp/tg_probe28_29e501943ec34ce7a04c08fe8bc5eb1d/B/packet28_matrix_results.json`
   - Failed assertions summary: 24/24
5. Attempt C (Option C prototype, strict canonical explicit-workdir restriction):
   - Build: PASS
   - Matrix result: FAIL
   - Result JSON: `C:/Users/wd985049/AppData/Local/Temp/tg_probe28_29e501943ec34ce7a04c08fe8bc5eb1d/C/packet28_matrix_results.json`
   - Failed assertions summary: 24/24

## Decision
1. A8.0 is FAIL for this packet line.
2. No migration option (A/B/C) met required matrix assertions within the three-attempt bound.
3. Stop condition remains active; keep packet 27 (A8) blocked and keep A9 blocked.
4. Next required step is a new architecture decision packet before any A8 implementation resumes.
