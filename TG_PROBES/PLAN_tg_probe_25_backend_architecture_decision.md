# PLAN_tg_probe_25_backend_architecture_decision
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Planning/research packet authored from packet 24 evidence and read-only source/CMake inspection; no implementation changes made; A8 remains closed **high**

## Purpose
Select the backend architecture checkpoint after A7.1 packet 24 disproved bounded mtproto source partition options A/B. This packet compares:
1. Option A: standalone TDLib tg_cli.
2. Option B: low-level decoupled MTProto client.
3. Option C: console/headless mode hosted inside or alongside fully linked Telegram runtime (Core::Application/Profile graph), with windows suppressed via capabilities and staged stripping later.

It also evaluates an optional small IPC frontend/backend split only if existing single-instance IPC can support it without concurrent profile access.

## Locked Inputs
1. Packet 24 selected Option C (architecture-decision required), and consumed bounded probe attempts for mtproto config partitioning.
2. Probe 11 failed selected-source closure with 153 unresolved externals.
3. Fallback notes for Option B/TDLib are recorded in NOTE_tg_fallback_architectures.md.
4. A8 is closed until this packet chooses a backend direction and emits the next bounded executable checkpoint.

## Read-Only Architecture Inspection (Current Repository)

### 1. Build target topology and reuse feasibility
Evidence:
1. Telegram/CMakeLists.txt defines `add_executable(Telegram ...)` and links full desktop dependency closure.
2. BUILD_TG_CLI exists as a guarded `add_subdirectory(tg_cli)`.
3. tg_cli currently builds as a minimal independent target with QtCore only and isolated tg_cli sources.

Findings:
1. A second fully linked executable can be added in CMake, but current desktop source ownership is attached directly to Telegram executable and related targets, not as one reusable object closure for a second app entry by default.
2. Producing a second fully linked executable with nearly the same source closure would likely duplicate compilation work unless a larger CMake refactor introduces shared object/static aggregation for large SourceFiles clusters.
3. A `-console` mode in the existing `tg` executable avoids duplicate object compilation and is the safer first executable checkpoint for proving hosted-runtime viability.

### 2. Entrypoint/runtime host chain
Evidence:
1. Telegram/SourceFiles/main.cpp -> Core::Launcher::Create(...)-> launcher->exec().
2. Core::Launcher::exec() constructs Sandbox via executeApplication().
3. Core::Sandbox subclasses QApplication and creates Core::Application in launchApplication().

Findings:
1. Existing desktop runtime startup is anchored to Launcher/Sandbox/Application and single-instance handling before Core::Application run.
2. Any hosted console mode that wants existing profile/auth graph fidelity gets that fastest by entering this chain and gating UI/presentation behavior with capabilities/mode guards.
3. A pure QCoreApplication CLI path remains cleaner long term, but packet 24 + probe 11 evidence indicates it is not currently bounded for the needed Main/Storage/MTP graph.

### 3. Executable naming and packaging constraints
Evidence:
1. Telegram/CMakeLists.txt sets desktop output name to `tg` (fenced block).
2. tg_cli target sets output name to `tg_cli`.

Findings:
1. Naming already supports side-by-side binaries (`tg`, `tg_cli`) if/when a second fully linked executable is justified.
2. For the earliest hosted-runtime checkpoint, adding `-console` mode to `tg` minimizes CMake/output churn and keeps packaging behavior stable.

### 4. Existing single-instance IPC and profile ownership signal
Evidence from core/sandbox.cpp and core/sandbox.h:
1. Single-instance local server name is derived from working-dir hash.
2. IPC command handling is command-string based (`CMD:show`, `CMD:quit`, `OPEN:`, `CTRL:`) and tied to a single primary process.
3. Locking and single-instance checks are integrated before application launch.

Findings:
1. Current IPC is oriented toward activation/control of one primary app instance, not a general high-throughput frontend/backend RPC surface.
2. A tiny frontend/backend split is only safe if the backend is the sole profile owner and frontend never touches profile storage.
3. Existing IPC primitives are insufficient today as the main architecture decision vehicle for V0; adapting them into a robust CLI protocol is additional architecture work and not a faster path than hosted mode.

## Decision Matrix

Scoring keys:
- Effort/risk: lower is better.
- Shared profile compatibility: ability to reuse desktop tdata/auth safely.
- First runnable time: time to first executable V0 checkpoint.
- UI stripping trajectory: viability of Stage 7 incremental stripping.
- Protected edit surface: smaller is better for mergeability.

| Option | Effort | Risk | Shared profile compatibility | First runnable time | UI stripping trajectory | Protected edit surface |
|---|---|---|---|---|---|---|
| A. TDLib standalone tg_cli | Medium | Medium | No (separate auth/profile) | Fast for independent CLI, slow for shared-profile goals | Strong (already headless API) | Minimal |
| B. Low-level decoupled MTProto | Very high | High | Possible in theory, unproven and costly parser/model duplication | Slowest (new auth/storage/model/update ownership) | Medium (custom model can be headless) | Minimal |
| C. Hosted runtime mode (inside tg or second fully linked executable) | Medium | Medium | Yes (best fit with existing Core::Application/Profile graph) | Fastest for shared-profile V0 | Good staged path (retain UI linkage first, strip later) | Moderate (capability seams already accepted) |

Optional D check (not selected as primary architecture):
| Option | Effort | Risk | Shared profile compatibility | First runnable time | UI stripping trajectory | Protected edit surface |
|---|---|---|---|---|---|---|
| D. Small IPC frontend/backend split on current single-instance channel | Medium-high | High | Potentially yes only with strict single-owner backend | Slower than C for V0 due IPC protocol expansion | Neutral | Moderate-high |

## Architecture Decision
Decision: Option C is selected.

Why this is decisive from current evidence:
1. Option A cannot satisfy the locked desktop tdata/auth reuse goal.
2. Option B remains the highest cost/uncertainty path and was already preceded by compile-closure evidence that desktop internals are tightly coupled.
3. Option C aligns with current capability extraction direction (A6 complete), preserves desktop profile compatibility, and can reach a runnable V0 checkpoint sooner by accepting temporary UI linkage and stripping later (Stage 7).

## Hosted Option C Sub-Choice (First Checkpoint)
First checkpoint choice: `-console` mode on `tg` executable, not a second fully linked executable yet.

Rationale:
1. Avoids duplicate object compilation and large CMake source-list refactor at checkpoint time.
2. Reuses current Launcher/Sandbox/Application pipeline immediately.
3. Keeps binary naming/packaging stable while proving runtime behavior.

Follow-on path:
1. If checkpoint succeeds, decide whether to keep mode-in-`tg` for V0 or split to `tg_cli_full` later for operator clarity.
2. Keep existing minimal tg_cli skeleton target as independent command-shell staging area only if needed; do not force backend binding there before closure proof.

## A8 Gate State
1. A8 remains CLOSED in this packet.
2. No profile-open/read probe is authorized until the next bounded executable checkpoint demonstrates hosted backend initialization and ownership boundaries.

## Exact Next Implementor Packet/Probe
Next packet to author and execute:
`TG_PROBES/PLAN_tg_probe_26_a7_3_hosted_console_mode_checkpoint.md`

Required scope of packet 26:
1. Implement the smallest bounded executable probe for Option C using `tg -console` mode.
2. Confirm entry through Launcher/Sandbox/Application without opening desktop windows (or with deterministic suppression hooks).
3. Prove profile ownership gate behavior remains fail-closed and single-owner.
4. Keep A8 closed unless packet 26 explicitly passes all checkpoint criteria.

## Validation Record (for this packet)
1. Read-only docs and source/CMake inspection only.
2. No source or behavior implementation edits.
3. No profile access.
