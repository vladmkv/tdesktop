# PLAN_tg_probe_23_a7_cli_bundle_synthetic_construction
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [BLOCKED] A7 blocked cleanly at Gate 1 after bounded expansions consumed (3/3); Option A selected for A7qq (stop and emit next decision packet), failed/uncommitted A7 wiring restored to A6 commit `d7daf02d01`, and tg_cli baseline remains functional **high**

## Goal
Deliver the first runnable technical A7 checkpoint for tg_cli by wiring a CLI Domain capability bundle and internal account-capability factory, then proving bounded synthetic Domain construction/start behavior without real profile access.

This packet defines implementation boundaries and decision gates only. It intentionally does not advance A8 profile ownership or any user-facing commands.

## Locked Inputs Carried Forward
1. Probe 11 failed bounded selected-source closure after three expansions with 153 unresolved externals.
2. Probe 12 and Probe 13 were blocked because no bounded executable model existed.
3. A6 completed desktop seam extraction and owner account-factory routing.
4. A6 bounded linkage attempts did not converge:
- Broad td_mtproto linkage pulled AbstractConnection, Instance, Logs, and tl::utf16 closure.
- Narrow mtproto_config.cpp expansion reached base include chain.
- Third expansion reached mtproto prelude/generated scheme closure.

## A7 Non-Negotiable Constraints
1. Maximum three linkage expansions in A7 closure work.
2. No real profile access and no desktop default profile probing.
3. Synthetic temporary workdir only.
4. No broad UI closure and no window construction path.
5. No lib_* edits.
6. Any protected-source change must be fenced and justified by a specific unresolved dependency.
7. A8 profile ownership, passcode, and account-selection work is out of scope.

## Ordered Decision Gates

### Gate 0: Baseline and scope lock
Entry criteria:
1. A6 seam state is present and unchanged in intent.
2. tg_cli skeleton path remains functional.

Exit criteria:
1. A7 implementation scope is constrained to CLI bundle/factory, synthetic construction, runtime class decision, and dependency identification.
2. No command surface or profile flow is added.

### Gate 1: Proven linkable fallback-config provider
Question:
Can CLI lifecycle/account capabilities obtain fallback production config from a provider that links in tg_cli without reopening broad mtproto closure?

Option order (mandatory):
1. Prefer an already desktop-linked reusable target/API that is already proven in this branch.
2. If none works within bounded attempts, allow a tiny protected factory seam in an owning upstream mtproto abstraction that returns the needed config provider object/value.

Rules:
1. The tiny seam is allowed only after Option 1 is disproved.
2. The seam must be in an owning abstraction, not in lib_* and not in generated files.
3. The seam must have its own fence IDs, rationale, and rollback note.

Fail/stop rule:
If no linkable provider is proven within three linkage expansions, stop A7 and author a new executable-closure decision packet.

A7q. [DONE] CLARIFY (high) -- Gate 1 Option 1 executed as Option A (continue with bounded expansion 2) and proceeded to final expansion 3 fallback seam attempt.
	> Outcome: Expansion 2 fixed compile-context closure and reached bounded link closure. Expansion 3 added one minimal fenced fallback-config factory seam in owning mtproto abstraction, but tg_cli link still failed on broad unresolved mtproto symbols (AbstractConnection, Logs, MTP::Instance internals, tl::utf16). Expansion-3 seam wiring was rolled back.

A7qq. [DONE] CLARIFY (high) -- Gate 1 resolved as Option A: stop A7 after bounded expansions are exhausted (3/3) and emit the next executable-closure decision packet; do not grant a packet-cap policy exception.
	> Context: Post-rollback Gate 1 build still fails in `tg_cli` with 11 unresolved externals from broad mtproto closure (`mtproto_dc_key_creator.obj` AbstractConnection symbols, `mtproto_received_ids_manager.obj` Logs symbols, `mtproto_concurrent_sender.obj` MTP::Instance symbols, `mtproto_config.obj`/`mtproto_dc_options.obj`/`mtproto_response.obj` `tl::utf16`). Expansion count consumed: 3/3.
	> Options considered: A) Stop A7 and author next executable-closure decision packet (packet-compliant), B) Add explicit exception and continue closure expansion despite packet cap.
	> Decision: Option A selected. A7 remains blocked and closed for implementation until the next packet proves a minimal isolated path.

### Post-block Cleanup Proof (implementation wiring only)
1. Restored to A6 commit `d7daf02d01` state:
- `Telegram/tg_cli/CMakeLists.txt`
- `Telegram/tg_cli/capabilities/account_network_capabilities.h`
- `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h`
2. Deleted untracked partial A7 source:
- `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities_cli.cpp`
3. Planning history retained unchanged in `PLAN_tg_console_mode.md` and TG_PROBES packets except explicit A7 decision/status updates.
4. Exact failure evidence retained unchanged in this packet under Gate 1 context.

### Next Packet Handoff
Next decision/probe packet: `PLAN_tg_probe_24_a7_1_minimal_mtproto_config_target.md`.
A8 remains closed until packet 24 proves a bounded minimal compile/link path.

### Gate 2: CLI bundle/factory wiring
Do only after Gate 1 passes.

Required A7 outputs:
1. `CreateCliDomainCapabilityBundle()` declaration and implementation.
2. Internal CLI domain account factory implementation that creates fresh per-account bundles.
3. Shared CLI fallback production config state used by:
- CLI DomainLifecycleCapabilities fallback-copy behavior.
- CLI AccountNetworkCapabilities fallback-copy behavior.
4. CLI never-proxy behavior in account network capabilities:
- `proxyChanges()` returns `rpl::never<ProxyChange>()`.
- proxy notification and rotation hooks are explicit no-ops.

Design guardrails:
1. Keep CLI account-network factory internals private unless a true C++ boundary requires a public symbol.
2. Preserve desktop behavior and existing constructor compatibility.
3. No command handling logic in this gate.

### Gate 3: Synthetic Domain construction/start boundary
Do only after Gate 2 passes.

Required scope:
1. Create a synthetic temporary workdir boundary for tg_cli technical probing.
2. Construct Domain through the CLI bundle path.
3. Exercise a bounded start/teardown path without requiring real profile data.

Prohibited scope:
1. Opening desktop profile locations.
2. Mutating real tdata.
3. Enabling multi-account/session command flow.

### Gate 4: Runtime substrate decision (QCoreApplication vs QApplication)
Question:
What minimal Qt application class is required for the synthetic A7 construction path?

Decision rule:
1. Attempt QCoreApplication first.
2. Escalate to QApplication only if QCoreApplication is blocked by concrete runtime or initialization requirements.
3. Do not treat UI/controller construction as acceptable evidence; that is a stop condition.

Outcome requirement:
Record one selected runtime class and the exact observed blocker if escalation was required.

### Gate 5: Explicit MTP::Instance/Core::App dependency identification before Session
Question:
What exact Core::App-coupled dependencies remain on the path to constructing Session?

Required output:
1. A precise dependency list with symbol/file ownership categories.
2. Clear boundary marker: Domain+Account proven versus Session blocked/unblocked.
3. If still blocked, stop before Session implementation and emit next decision packet input.

Hard rule:
Do not attempt Session construction until this dependency inventory is produced.

## Bounded Linkage Expansion Policy (A7)
1. Expansion count is global for A7 closure work and capped at three.
2. Every expansion must record:
- what was added or changed,
- which unresolved set it targeted,
- why it is still bounded.
3. Expansions cannot broaden into generic desktop UI closure.
4. If expansion three does not produce a proven linkable provider path, stop.

## Protected Seam Fallback Policy
Only if Gate 1 Option 1 fails:
1. Add one tiny seam in an owning upstream mtproto abstraction.
2. Keep seam intent narrowly scoped to fallback-config provider acquisition.
3. Fence this seam separately from other A7 edits.
4. Include explicit justification that existing reusable targets/APIs were exhausted within bounded expansions.
5. No lib_* edits.

## First Runnable Technical Checkpoint (A7-TR1)
Checkpoint definition:
tg_cli technical path can initialize and tear down synthetic Domain construction via CLI bundle wiring, with deterministic runtime class selection and no real profile usage.

Exact acceptance criteria:
1. CLI Domain capability bundle factory path is callable and returns non-null lifecycle and account-factory capabilities.
2. Internal CLI account factory yields fresh non-null network/storage/session capability bundles per call.
3. Shared fallback production config state is consumed by both CLI lifecycle and CLI account-network capability implementations.
4. Synthetic temporary workdir path is used exclusively for the checkpoint run.
5. No access to desktop default profile or user real workdir occurs.
6. No window/controller construction is required.
7. One runtime class decision is recorded: QCoreApplication or QApplication.
8. MTP::Instance/Core::App dependency inventory is captured before any Session-attempt branch.
9. If Session is blocked, blocker is explicit and A8 remains closed.
10. Fence and diff hygiene checks pass.

## Stop Conditions
1. Linkage closure exceeds three expansions.
2. Any required change touches lib_*.
3. A reusable-provider path cannot be proven and tiny-seam fallback is either unjustified or still non-linkable.
4. Synthetic path requires real profile data to proceed.
5. Construction requires broad UI/window closure.
6. Session attempt begins before MTP::Instance/Core::App dependency inventory is documented.
7. Fence policy compliance cannot be maintained.

## Out Of Scope
1. Profile ownership lock acquisition logic.
2. Passcode prompts and wrong-passcode taxonomy.
3. Account selection UX and status command.
4. Chat/history/send/edit/delete behaviors.
5. Any A8+ implementation work.

## Evidence Required In A7 Completion Record
1. Files changed and fence IDs touched.
2. Gate outcomes (0 through 5) with pass/stop disposition.
3. Linkage expansion log with final count.
4. Runtime class decision and justification.
5. MTP::Instance/Core::App dependency inventory result.
6. A7-TR1 acceptance checklist with pass/fail per item.
7. Explicit statement that no real profile access occurred.
8. Explicit statement that no broad UI closure was introduced.
