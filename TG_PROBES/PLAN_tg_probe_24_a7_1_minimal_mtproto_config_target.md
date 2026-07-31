# PLAN_tg_probe_24_a7_1_minimal_mtproto_config_target
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Executed as bounded decision/probe packet. Probe 0 dependency inventory completed first; Option A and B were disproven by isolated compile evidence; Option C selected with architecture decision required before any further A7 implementation work **high**

## Purpose
Investigate whether mtproto config can become a dedicated minimal reusable target or additive source partition for tg_cli without arbitrary closure expansion, without lib_* edits, and while preserving upstream source behavior.

This packet is intentionally planning-only. It defines a strict read-only dependency inventory first, then bounded compile/link probes to select one path.

## Locked Inputs
1. A6 implementation baseline is commit `d7daf02d01`.
2. A7 packet 23 consumed the bounded expansion budget (3/3) and is blocked at Gate 1.
3. Failed/uncommitted A7 wiring has been restored to baseline:
- `Telegram/tg_cli/CMakeLists.txt`
- `Telegram/tg_cli/capabilities/account_network_capabilities.h`
- `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h`
4. Untracked partial source was removed:
- `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities_cli.cpp`
5. Exact unresolved-symbol evidence from packet 23 remains authoritative and must not be rewritten.

## Non-Negotiable Constraints
1. Read-only dependency graph and source-level dependency inventory must be completed before any implementation change.
2. No profile access and no desktop default profile probing.
3. No `MTP::Instance`, `MTP::details::AbstractConnection`, `Logs::*`, or UI/window construction in this packet.
4. No lib_* edits.
5. No broad source closure expansion.
6. Maximum three probe attempts total in this packet.
7. A8 remains closed and out of scope.

## Required Read-Only Inventory (must run first)
1. Build a read-only dependency graph for tg_cli and relevant mtproto/object ownership.
2. Capture exact source-level dependencies (headers, direct symbol providers, generated prerequisites, target ownership) for:
- `Telegram/SourceFiles/mtproto/mtproto_config.cpp`
- `Telegram/SourceFiles/mtproto/mtproto_dc_options.cpp`
- `Telegram/SourceFiles/mtproto/mtproto_proxy_data.cpp`
- `Telegram/SourceFiles/mtproto/mtproto_response.cpp`
3. Classify each dependency as:
- local source in `Telegram/SourceFiles/mtproto`
- generated scheme/prelude dependency
- dependency from desktop-app::* targets
- dependency that would pull blocked runtime classes (`MTP::Instance`, `AbstractConnection`, `Logs`)
4. Record whether each file can compile in isolation with bounded includes and without generated/global runtime closure.

## Options To Compare

### Option A: Minimal reusable target or additive source partition
Goal:
- Create a minimal reusable OBJECT or STATIC target in `Telegram/cmake` or an additive source partition that provides only the required mtproto config-related objects.

Hard requirements:
1. Preserve upstream source files; no behavior edits inside mtproto implementation files unless explicitly justified by probe results.
2. Remove those objects from `td_mtproto` or share them in a way that avoids duplicate symbols.
3. Keep linkage bounded to proven compile/link closure from this packet only.
4. No dependency path may drag in blocked runtime classes.

### Option B: Additive tiny serialized/default-config value factory seam
Goal:
- Add one tiny seam that exposes serialized/default config value production for tg_cli without exposing `MTP::Config` object ownership to tg_cli until full runtime closure is proven.

Hard requirements:
1. Seam stays narrowly scoped to config value factory behavior.
2. Do not expose or construct full runtime-coupled mtproto objects in tg_cli.
3. No profile interactions.
4. No lib_* edits.

### Option C: Declare backend architecture blocked and reconsider fallback backend
Goal:
- If options A and B cannot be proven in bounded probes, declare current backend path blocked and evaluate fallback architecture path:
- TDLib backend
- lower-level MTProto path decoupled from desktop runtime assumptions

Hard requirements:
1. Capture explicit blocker categories with source/symbol ownership.
2. Do not continue incremental closure expansion inside this packet.

## Probe Method

### Probe 0 (read-only)
1. Gather the dependency graph and exact per-file source dependency inventory.
2. Produce a closure map showing what each of the four mtproto files requires to compile and link.
3. Decide which option (A or B) is most plausible for minimal isolated probing.

### Probe 1-3 (bounded implementation probes)
Rules for each attempt:
1. Define one minimal hypothesis.
2. Modify only files required for that hypothesis.
3. Compile/link only the smallest probe target needed to validate the hypothesis.
4. Record exact unresolved symbols and owning object files/targets.
5. Stop after three attempts total even if partially improved.

Absolute stop triggers:
1. Any probe pulls `MTP::Instance`, `AbstractConnection`, `Logs::*`, or UI/window runtime requirements.
2. Any probe requires profile reads/writes.
3. Any probe requires lib_* modifications.
4. Duplicate symbol resolution requires broad target redesign beyond packet scope.

## Decision Rule
1. Choose Option A if a minimal reusable target/source partition is proven compile/link viable without blocked runtime dependencies and without duplicate symbols.
2. Choose Option B if Option A is disproven but a tiny serialized/default-config factory seam is proven compile/link viable within constraints.
3. Choose Option C if neither A nor B is proven within three attempts.

## Acceptance Criteria
1. Read-only dependency graph completed first and included in evidence.
2. Exact source-level dependency inventory captured for all four mtproto files.
3. At least one option is tested with isolated compile/link evidence.
4. Final selected option is justified by concrete probe evidence, not by conjecture.
5. No profile access occurred.
6. No blocked runtime classes were introduced.
7. No duplicate symbols introduced in the proven path.
8. A8 remains closed unless this packet explicitly records a proven path.

## Out Of Scope
1. Domain/Account/Session runtime construction.
2. Passcode/account selection/profile ownership workflows.
3. Command-surface changes.
4. Any A8 or later implementation work.

## Evidence Required In Completion Record
1. Read-only dependency graph output and method used.
2. Source-level dependency matrix for mtproto config/dc_options/proxy/response.
3. Attempt log for each probe (max 3).
4. Exact compile/link outputs and unresolved symbol ownership.
5. Final decision (A, B, or C) with rationale.
6. Statement that A8 remained closed throughout.

## Completion Record

### Probe 0 Read-Only Dependency Graph (completed first)
Method:
1. `cmake --graphviz=%TEMP%\\tg_probe24_graph.dot -S . -B out`
2. Filtered graph extract for `tg_cli`, `td_mtproto`, `td_scheme`, and direct substrate targets.

Exact dependency graph extract (focused):
- `tg_cli -> Qt5::Core`
- `tg_cli -> common_options`
- `td_mtproto -> td_scheme`
- `td_mtproto -> external_zlib`
- `td_scheme -> lib_base`
- `td_scheme -> lib_tl`
- `Telegram -> td_mtproto`
- `Telegram -> td_scheme`

Read-only ownership facts:
1. All four candidate files are owned by `td_mtproto` object target (`Telegram/cmake/td_mtproto.cmake`).
2. `td_mtproto` has precompiled header `mtproto/mtproto_pch.h` which force-loads `scheme.h` and `logs.h` prerequisites.
3. Baseline `tg_cli` has no `td_mtproto` link edge.

### Source-Level Dependency Matrix (four required files)

1. `mtproto_config.cpp`
- Local mtproto dependency: `mtproto_config.h` -> `mtproto_dc_options.h`.
- Generated/prelude dependency: types/functions/macros from `scheme.h` path (`MTPDconfig`, `mtpIsTrue`, reaction variants, `qs`, `Fn/FnMut` family through prelude).
- desktop-app/other target dependency: `storage/serialize_common.h`, QtCore; base/rpl through headers.
- Blocked runtime coupling observed: uses `LOG` and `DEBUG_LOG` macros from `logs.h` (blocked `Logs::*` family).
- Isolation verdict: not isolated-compilable without mtproto prelude and logging substrate.

2. `mtproto_dc_options.cpp`
- Local mtproto dependency: `mtproto_dc_options.h`, `details/mtproto_rsa_public_key.h`, `facade.h`, `connection_tcp.h`.
- Generated/prelude dependency: mtproto scheme types (`MTPDcOption`, `MTPDcdnConfig`, `DcId`, flags).
- desktop-app/other target dependency: `storage/serialize_common.h`, QtCore/QtNetwork, base/rpl.
- Blocked runtime coupling observed: `connection_tcp.h` inherits from `AbstractConnection` and carries `not_null<Instance*>`; cpp also uses logging macros (`Logs::*` path).
- Isolation verdict: fails bounded isolation; drags blocked runtime classes (`AbstractConnection`, `MTP::Instance`) and logging substrate.

3. `mtproto_proxy_data.cpp`
- Local mtproto dependency: `mtproto_proxy_data.h`.
- Generated/prelude dependency: none direct to `scheme.h`, but relies on prelude-provided aliases/types (`bytes`, `crl::time`, assertion macros).
- desktop-app/other target dependency: QtNetwork (`QNetworkProxy`), base helpers.
- Blocked runtime coupling observed: no direct `MTP::Instance`/`AbstractConnection`/`Logs` reference in this TU.
- Isolation verdict: potentially narrow at TU level, but header still requires broader prelude/type substrate.

4. `mtproto_response.cpp`
- Local mtproto dependency: `mtproto_response.h`.
- Generated/prelude dependency: strong; requires `MTPrpcError`, `mtpBuffer`, `MTP_rpc_error`, `MTP_int`, `MTP_bytes`, `qs` from generated/prelude path.
- desktop-app/other target dependency: QtCore (`QRegularExpression`, `QDebug`).
- Blocked runtime coupling observed: no direct `MTP::Instance`/`AbstractConnection`/`Logs` in TU body.
- Isolation verdict: not isolated-compilable without generated scheme/prelude closure.

### Attempt Log (max 3, bounded)

Attempt 1 (Option A):
1. Hypothesis: Add four mtproto files to `tg_cli` as minimal additive source partition with bounded links.
2. Probe wiring: temporary add of `mtproto_config.cpp`, `mtproto_dc_options.cpp`, `mtproto_proxy_data.cpp`, `mtproto_response.cpp`; add minimal include path and narrow link candidates.
3. Probe command: `cmake --build out --config Debug --target tg_cli`.
4. Result: FAIL (compile stage).
5. Exact failure class: widespread prelude/type closure errors (`MTPrpcError`, `qs`, `bytes`, `crl`, struct field/type collapse) before link; no bounded isolated partition proven.

Attempt 2 (Option B):
1. Hypothesis: Tiny serialized/default-config seam can be compiled by limiting probe to config + dc_options and one TG-owned value factory TU.
2. Probe wiring: temporary probe TU (`BuildDefaultSerializedMtprotoConfig`), limited mtproto sources (`mtproto_config.cpp`, `mtproto_dc_options.cpp`), forced include prelude attempt.
3. Probe command: `cmake --build out --config Debug --target tg_cli`.
4. Result: FAIL (compile stage).
5. Exact failure class: unresolved foundational mtproto/base/rpl type ownership in `mtproto_dc_options.h` (`DcId`, `MTPDcOption`, `base::flat_map`, `rpl::event_stream`) indicating seam still requires broad prelude/runtime closure.

Attempt 3 (Option C guard and restoration check):
1. Hypothesis: Neither A nor B is proven within bounded probes; restore baseline and validate no residual wiring.
2. Probe command: `cmake --build out --config Debug --target tg_cli`.
3. Result: PASS (`ATTEMPT3_EXIT_CODE=0`) after probe rollback.
4. Runtime smoke: `out\\Telegram\\tg_cli\\Debug\\tg_cli.exe --help` PASS.

### Final Decision
Selected option: C.

Rationale:
1. Option A disproven by isolated compile closure failure before link and by direct blocked-coupling path in `mtproto_dc_options.cpp` via `connection_tcp.h` (`AbstractConnection` and `MTP::Instance`) plus `Logs::*` macros.
2. Option B disproven by inability to keep a tiny seam independent from foundational generated/prelude and mtproto base type closure.
3. Bounded attempt budget (3/3) consumed; no additional expansion allowed in this packet.

### Implementor-Ready Next Step (exact)
1. Author and execute a new architecture-decision packet `TG_PROBES/PLAN_tg_probe_25_backend_architecture_decision.md`.
2. Scope that packet to compare fallback backend directions only:
- TDLib backend path.
- Lower-level MTProto path explicitly decoupled from desktop runtime assumptions.
3. Required output of packet 25: chosen backend direction, ownership boundaries, and a first bounded executable probe packet for the chosen direction.

### Constraint Confirmation
1. No profile access occurred.
2. No Main/Storage protected runtime source was modified.
3. No lib_* files were modified.
4. No `MTP::Instance` / `AbstractConnection` / `Logs` / UI closure was introduced into final state.
5. A8 remained closed throughout.
