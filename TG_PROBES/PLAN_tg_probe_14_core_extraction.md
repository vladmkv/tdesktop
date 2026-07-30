# PLAN_tg_probe_14_core_extraction
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_fallback_architectures.md
Status: [TODO] A0/A1/A2/A3/A4/A5/A6 completed; A7 executable closure packet pending **high**

## Architecture
Permit a bounded protected-source refactor that exposes four narrow capability seams shared by tg and tg_cli.

All protected-source changes follow ../TG_CHANGE_POLICY.md and use named TG_CHANGE fences.

## Locked Tradeoff
- Preserves desktop tdata/session reuse.
- Violates the prior no-protected-source-edit rule.
- Carries ongoing merge conflict risk in Main/Storage/Core files.
- Existing desktop constructors and public APIs remain unchanged; TG adds overloads or optional capability accessors.
- No fixed line budget; semantic stop conditions control scope.

## Why One Host Interface Was Rejected
- Main::Domain alone contains roughly two dozen Core::App calls spanning unrelated responsibilities.
- A single ISessionHost would become a second Core::Application and grow with upstream features.
- Main::Domain cannot simply be bypassed because Storage::Domain requires a Main::Domain owner and creates Main::Account objects through it.

## Capability Design

### 1. AccountNetworkCapabilities
Used by Main::Account only.

Responsibilities:
- fallback production MTP configuration
- proxy change stream/current proxy state
- proxy rotation checks

Desktop implementation forwards to Core::Application.
CLI implementation owns only the required network/config state.

### 2. SessionServiceCapabilities
Used by Main::Session only.

Responsibilities:
- download-session tracking
- setup-email lock/unlock notifications
- optional window lookup for desktop-only behavior

Window methods return null in CLI. Any code that cannot tolerate null is a stop signal, not permission to create a CLI window.

### 3. StorageSettingsCapabilities
Used by Storage::Account only.

Responsibilities:
- night/day and chat-background tile settings
- tonsite storage token read/write
- delayed settings persistence

CLI implementation stores only required non-UI values in TG-owned settings; it must not mutate desktop UI settings accidentally.

### 4. DomainLifecycleCapabilities
Used by Main::Domain only.

Responsibilities are split into required lifecycle and optional desktop presentation:
- account ordering/active-account persistence
- fallback config refresh
- scheduling/postponed calls
- passcode-lock state transitions
- optional notification/window/export presentation hooks

Every optional presentation hook must be explicit and no-op capable. If domain logic requires a real Window::Controller, the extraction fails.

### Capability Bundle
Main::Domain receives an additive constructor overload accepting an owning Domain capability bundle (lifecycle capability plus per-account capability factory). The existing `Domain(const QString&)` remains and delegates to desktop capability-bundle selection, preserving existing call sites.

Main::Account and Storage::Account receive capabilities transitively from their owner. Existing constructors remain available and preserve desktop behavior.

## Fence Layout

Existing headers:
- Fence each forward declaration, overload, accessor, and member addition separately only when they are not adjacent; otherwise use one cohesive block.

Existing implementations:
- Fence additive overload/delegating constructor blocks.
- Fence each replaced global access at the smallest cohesive statement block.
- Use stable IDs such as:
	- `domain-capability-overload`
	- `account-network-capabilities`
	- `session-service-capabilities`
	- `storage-settings-capabilities`

Existing CMake:
- Use `# TG_CHANGE_BEGIN/END` markers.
- Current uncommitted BUILD_TG_CLI, add_subdirectory, and output rename must be retrofitted before commit.

New TG-owned capability implementations and checker files require no fences.

## Granular Probes

### A0: Fence Enforcement Baseline
Status: [PARTIAL] Checker and CMake fences exist, but full branch validation against `12e8d4a956` fails on unfenced AGENTS.md and dav1d changes; mixed-hunk deletion coverage is missing.
1. Add Telegram/tg_cli/tools/check_tg_change_fences.py.
2. Retrofit named CMake fences around current uncommitted changes.
3. Run checker against branch base.
4. Build tg_cli skeleton.

Pass:
- Checker passes and deliberately injected test violations fail.
- tg_cli still builds/runs.

### A0.1: Fence Enforcement Corrections
Status: [DONE] Completed on tg-cli; full-branch checker PASS against `12e8d4a956`.
Implementor packet: PLAN_tg_probe_18_a0_1_fence_corrections.md
1. Validate deletions in mixed hunks, not only deletion-only hunks.
2. Add mixed-hunk/adjacent-block/duplicate-ID self-tests.
3. Fence TG additions in AGENTS.md and the dav1d mirror replacement in prepare.py.
4. Remove the historical dav1d checker exception from policy.
5. Require full checker PASS against `12e8d4a956`.

### A1: Capability Inventory (read-only)
Status: [DONE] Completed in NOTE_tg_probe_14_a1_inventory.md with per-call-site classification, lifecycle tags, methods, and fence IDs.
1. Classify every Core::App/Window::Theme call in target files into one of four capabilities.
2. Record required versus optional presentation behavior.
3. Reject any operation that does not fit one cohesive capability.

Pass:
- No fifth broad catch-all capability is needed.

### A2: Interface Types + Desktop Implementations
Status: [DONE] Interfaces/desktop forwarders and fenced CMake wiring committed; desktop Debug build and startup/connect smoke validated in A2.1.
1. Add TG-owned capability interface files.
2. Add desktop forwarding implementations.
3. Wire files into the desktop build inside fenced CMake blocks only.
4. Do not inject them into Main/Storage yet.

Pass:
- Desktop Debug builds/runs with no behavior change.
- Fence checker passes.

### A2.1: Capability Baseline Corrections
Status: [DONE] Completed on tg-cli with precondition record and single-attempt desktop validation.
Implementor packet: PLAN_tg_probe_19_a2_1_baseline_corrections.md
1. Include `crl::time` from its direct declaration header.
2. Document intentional TG-owned ProxyChange DTO boundary.
3. Confirm no tg_cli path calls desktop factories that require Core::App().
4. Validate disk/process preconditions before desktop build.
5. Complete desktop Debug build and startup smoke test.

### A3: Main::Account Injection
Implementor packet: PLAN_tg_probe_17_a3_account_network_injection.md
Status: [DONE] Completed on tg-cli with fenced Main::Account capability injection and desktop/tg_cli validation.
1. Add an overload/accessor while preserving current constructor.
2. Replace only Main::Account network/config globals with AccountNetworkCapabilities.
3. Existing desktop constructor selects desktop capabilities.

Pass:
- Desktop proxy/config behavior smoke test passes.
- tg_cli probe can construct the account-network capability object.

### A4: Storage::Account Injection
Implementor packet: PLAN_tg_probe_20_a4_storage_settings_injection.md
Status: [DONE] Completed on tg-cli with storage capability ownership transfer and scoped global replacements.
1. Pass StorageSettingsCapabilities transitively from Main::Account.
2. Replace theme/token/settings globals.
3. Preserve existing constructor behavior.

Pass:
- Desktop profile opens and theme/token settings round-trip.
- No CLI test writes desktop tdata.

### A5: Main::Session Injection
Implementor packet: PLAN_tg_probe_21_a5_session_service_injection.md
Status: [DONE] Completed on tg-cli with SessionService capability ownership/borrowing injection and scoped global replacements; implementation commit `16babf664f`, review-fix commit `884cb247de`.
1. Pass SessionServiceCapabilities transitively from Main::Account.
2. Replace email-lock/download/window global calls.
3. Require null-safe optional window behavior.

Pass:
- Desktop session startup and window selection remain correct.
- CLI construction creates no windows.

### A6: Main::Domain Desktop Seam Injection
Implementor packet: PLAN_tg_probe_22_a6_domain_lifecycle_account_factory.md
Status: [DONE] Completed on tg-cli with Domain lifecycle capability ownership + owner account-factory routing for all four account creation paths; desktop seam only.
1. Add capability-bundle constructor overload; preserve `Domain(const QString&)`.
2. Convert required lifecycle calls first.
3. Convert optional presentation hooks one cluster at a time.
4. Add generic per-account `DomainAccountFactoryCapabilities` and bundle types.
5. Add a fenced Main::Domain owner account factory and route all four account construction paths through it.
6. Provide/validate desktop Domain capability bundle/factory only; do not declare or implement `CreateCliDomainCapabilityBundle()` in A6.

Pass:
- Desktop multi-account activation/window behavior passes.
- tg_cli remains unchanged and `--help` still passes.
- Static checks confirm fresh per-account bundle creation and single ownership transfer.

### A7: CLI Bundle + Synthetic Construction/Runtime Closure
1. Declare and implement `CreateCliDomainCapabilityBundle()` and CLI bundle/factory wiring.
2. Move shared CLI fallback `MTP::Config(MTP::Environment::Production)` state into A7 and share it across CLI lifecycle + per-account network capabilities.
3. Move CLI never-proxy producer behavior (`proxyChanges() -> rpl::never<ProxyChange>()`) and related no-op proxy hooks into A7.
4. Resolve tg_cli executable linkage/source closure required by CLI bundle/runtime behavior.
5. Use TG-owned capabilities and synthetic temporary workdir.
6. Construct Domain/Account/Session without real profile access.
7. Determine QCoreApplication versus QApplication requirement.

Pass:
- Clean construction/teardown, no windows, no profile mutation.

Stop condition:
- If `MTP::Instance` construction still requires `Core::App` coupling after bounded closure attempts, stop and produce a new executable closure decision packet before any A8 profile work.

### A8: Shared Profile Read Probe
1. Acquire tg-compatible profile ownership first.
2. Open an authenticated copied test profile read-only where possible.
3. Select one account and list one dialog page.

Pass:
- Correct account/dialog data, no concurrent tg access, no unintended writes.

## Stop Conditions
- A capability becomes a generic Core::Application mirror or mixes unrelated responsibilities.
- A fifth catch-all capability is required.
- Existing desktop call sites must all change instead of using additive overloads.
- Any required lib_* modification.
- Desktop behavior regression.
- Dialog-list proof requires Window::Controller construction.
- A capability block cannot be isolated cleanly inside named fences.
- Upstream behavior must be disabled globally rather than selected by capability implementation.

## Pass Criteria
- Desktop remains behaviorally unchanged.
- tg_cli opens shared profile safely and lists dialogs.
- Protected edit surface is bounded and documented.
- Every protected change is fence-checker compliant.
- Existing desktop APIs/call sites remain source-compatible.

## Validation
After each protected edit:
1. Run fence checker.
2. Build and smoke-run tg Debug.
3. Build/run the narrow tg_cli probe.
4. Record the capability block IDs touched and test outcome.

## Devlog
- 2026-07-30: A0 completed. Added Telegram/tg_cli/tools/check_tg_change_fences.py with parser and hunk enforcement; checker self-test confirms valid and intentionally invalid fence cases.
- 2026-07-30: Retrofitted Telegram/CMakeLists.txt fence IDs tg-cli-build-option, tg-cli-subdirectory, tg-executable-name and added tg-cli-capability-desktop-sources fenced block.
- 2026-07-30: A1 completed. Added TG_PROBES/NOTE_tg_probe_14_a1_inventory.md with full scoped mapping for main_account/main_session/main_domain/storage_account/storage_domain.
- 2026-07-30: A2 completed. Added TG-owned capability interfaces and desktop forwarding implementations under Telegram/tg_cli/capabilities/.
- 2026-07-30: tg_cli Debug build and run pass. Desktop Telegram build hit fatal error C1033 on vc143.pdb lock; follow-up desktop run/build must occur after lock is cleared.
- 2026-07-30: Review found A0 full-branch checker failure (unfenced AGENTS/dav1d), mixed-hunk deletion gap, direct-include fragility, and unverified desktop A2 behavior. Prior build history also contained `No space left on device`; validation blocker is environmental but not proven to be only a PDB lock.
- 2026-07-30: Added PLAN_tg_probe_17_a3_account_network_injection.md with exact ownership, overload, fence IDs, validation, and stop conditions.
- 2026-07-30: A0.1 completed via PLAN_tg_probe_18_a0_1_fence_corrections.md. Added per-deleted-line deletion anchors in checker hunk parsing, expanded self-tests (mixed replacement fail path, valid replacement pass path, adjacent blocks, duplicate IDs), fenced AGENTS.md (`tg-cli-agent-guidance`) and prepare.py dav1d stage (`dav1d-github-mirror`), removed obsolete policy exception, and validated with self-test PASS, base `12e8d4a956` PASS, prepare.py py_compile PASS, dav1d print-path (`p` then quit), and `git diff --check` PASS.
- 2026-07-30: A2.1 completed via PLAN_tg_probe_19_a2_1_baseline_corrections.md. Replaced transitive `base/timer.h` with direct `<crl/crl_time.h>`, documented `ProxyChange` as intentional TG-owned DTO boundary, verified tg_cli skeleton does not call `CreateDesktop*Capabilities`, retained raw fenced `target_sources` for tg_cli capability sources as intentional due `nice_target_sources` source-root mismatch, and validated with checker self-test PASS, checker base `12e8d4a956` PASS, tg_cli build/help PASS, desktop `Telegram` Debug build PASS on single attempt, desktop `out/Debug/tg.exe` startup with established TCP connection, and `git diff --check` PASS.
- 2026-07-30: A3 completed via PLAN_tg_probe_17_a3_account_network_injection.md in commit `4ff1e1b250`. Injected `AccountNetworkCapabilities` into `Main::Account` with additive constructor overload and preserved legacy constructor delegation. Replaced fallback config/proxy producer/proxy state-change global calls with capability methods under fence IDs `account-network-capability-forward-declaration`, `account-network-capability-overload`, `account-network-capability-member`, `account-network-capability-include`, `account-network-capability-constructor`, `account-network-fallback-config`, `account-network-proxy-changes`, and `account-network-proxy-state-change`. Validation: checker self-test PASS, checker base `12e8d4a956` PASS, tg_cli build/help PASS, desktop build succeeded after transient `C1033` lock retries, startup/connect smoke PASS with `-workdir C:/Users/wd985049/bin/Release`, and user-reported manual proxy toggle smoke PASS.
- 2026-07-30: Added PLAN_tg_probe_20_a4_storage_settings_injection.md. The packet preserves both A3 constructors, adds a two-capability overload, transfers storage-capability ownership into Storage::Account, preserves the no-argument TonSite helper through a capability-taking overload, and keeps the CLI implementation separate from the desktop Core::App() factory.
- 2026-07-30: Independently reviewed A4 commit `530dec607a`; no blocking defect found. Fence self-test and full branch validation pass, scoped storage globals are absent, and the only unexercised path is the explicitly recorded TonSite manual regression. Added PLAN_tg_probe_21_a5_session_service_injection.md using account ownership with a non-owning Session reference so session recreation does not require a capability factory.
- 2026-07-30: A4 completed via PLAN_tg_probe_20_a4_storage_settings_injection.md. Added Main::Account 5-arg overload (network + storage) and preserved existing 3-arg/4-arg constructor APIs through delegation. Added Storage::Account capability-taking overload and non-null owned `_settingsCapabilities`. Replaced scoped `Window::Theme`/`Core::App()` uses in `storage_account.cpp` with storage-capability methods and added TonSite overload that preserves the no-arg API used by `iv_instance.cpp`. Added TG-owned CLI storage implementation (`CreateCliStorageSettingsCapabilities`) and compiled it only into `tg_cli` with QtCore-only linkage. Validation: fence self-test PASS, fence base `12e8d4a956` PASS, `storage_account.cpp` direct-global search empty, tg_cli build/help PASS, desktop Debug build PASS, startup/connect smoke PASS with `-workdir C:/Users/wd985049/bin/Release`, manual day/night toggle + restart restoration PASS, TonSite manual path skipped, `git diff --check` PASS.
- 2026-07-30: A5 completed via PLAN_tg_probe_21_a5_session_service_injection.md. Added Main::Account 6-arg overload including SessionService capability ownership and kept existing constructors source-compatible through delegation. Added Main::Session non-owning capability borrowing from account accessor and replaced five scoped globals in `main_session.cpp` (setup-email lock/unlock, download session tracking, optional window selection). Added TG-owned CLI implementation (`CreateCliSessionServiceCapabilities`) and compiled it only into `tg_cli` with QtCore linkage. Validation: checker self-test PASS, checker base `12e8d4a956` PASS, scoped direct-global search empty, tg_cli build/help PASS, user-verified desktop build rerun PASS (`Telegram.vcxproj -> .../out/Debug/tg.exe`), automated startup/connect smoke PASS with established TCP connection and clean teardown, `git diff --check` PASS; ordinary download, upload-stop confirmation, and setup-email manual regressions not exercised in this packet.
- 2026-07-30: Independent A5 review found deferred setup-email lock in `main_session.cpp` capturing Session state via `crl::on_main([=] { ... })` without lifetime protection. Fixed in place by wrapping the deferred callback with `crl::guard(this, ...)` inside the existing `session-service-setup-email-lock` fence to preserve timing and capability behavior while making Session-lifetime access safe.
- 2026-07-30: A5 review fix committed as `884cb247de` and A5 marked fully complete for queue advancement.
- 2026-07-30: Authored PLAN_tg_probe_22_a6_domain_lifecycle_account_factory.md with exact Domain capability bundle ownership, owner account-factory design for all Storage::Domain construction paths, callback signature correction (`std::function` -> `Fn`/`FnMut`), fence IDs, validation, and A7 deferral.
- 2026-07-30: Completed read-only CLI config probe correction for A6 packet 22. Locked one CLI-owned shared fallback production config state (`MTP::Config(MTP::Environment::Production)`) consumed by CLI Domain lifecycle + per-account CLI network capabilities; removed planned public `CreateCliAccountNetworkCapabilities`/separate CLI account-network TU unless real C++ boundaries require them; added explicit `tdesktop::td_mtproto` narrow-link probe/stop condition; recorded `MTP::Instance` Core::App coupling as A7-only blocker concern.
- 2026-07-30: A6 pre-source linkage probe stopped per packet rule. Linking `tg_cli` with `tdesktop::td_mtproto` plus `tdesktop::td_scheme` failed (`cmake --build out --config Debug --target tg_cli`) with unresolved symbols requiring additional mtproto/core/logging closure (`MTP::details::AbstractConnection`, `MTP::Instance`, `Logs::*`, `tl::utf16`). No protected-source edits were made; packet 22 is marked blocked with CLARIFY question 2q.
- 2026-07-30: Executed bounded A6 linkage-only follow-up (max 3 expansions) and resolved packet 22 CLARIFY 2q to stop A6 here. Expansion 1 (only `mtproto_config.cpp`) failed compile on `base/bytes.h` include chain; expansion 2 (`mtproto_dc_options.cpp` plus `desktop-app::lib_base`/`desktop-app::lib_tl`) failed compile on missing mtproto prelude types (`DcId`, `MTPDcOption`, `base::flat_map`, `rpl::event_stream`); expansion 3 (`/FI mtproto_pch.h`) failed compile on missing generated `scheme.h`. Restored `Telegram/tg_cli/CMakeLists.txt` to best-known state by removing disproved broad `tdesktop::td_mtproto`/`tdesktop::td_scheme` linkage and removing temporary source wiring.
- 2026-07-31: Re-scoped packet 22 to unblock A6. Kept bounded linkage probe failures as recorded evidence, removed blocked/CLARIFY state, narrowed A6 to desktop Domain seam + owner account-factory routing, and explicitly moved CLI bundle/factory implementation, shared CLI fallback config state, never-proxy behavior, and tg_cli executable closure decisions to A7.
- 2026-07-31: A6 completed via PLAN_tg_probe_22_a6_domain_lifecycle_account_factory.md. Added `DomainCapabilityBundle`/`DomainAccountFactoryCapabilities` and desktop bundle factory, corrected lifecycle callback types to `Fn`/`FnMut`, preserved `Domain(const QString&)` via delegating overload, replaced scoped `Core::App`/`crl::on_main` use in `main_domain.cpp` with capability calls, routed all Storage::Domain and Main::Domain add account construction paths through `createAccountForStorage`, and validated with fence checker self-test PASS, fence checker base `12e8d4a956` PASS, tg_cli build/help PASS, desktop Telegram Debug build PASS, desktop smoke run with safe workdir and no tg_cli profile open, and `git diff --check` PASS.

## A0-A2 Review Disposition

Mandatory before A3:
- Fix mixed add/delete hunk enforcement and expand checker self-tests.
- Fence AGENTS.md and dav1d branch changes so full checker validation passes.
- Use the direct `crl/crl_time.h` include for `crl::time`.
- Complete desktop Debug build/startup validation after disk/process preconditions pass.

Intentional/no change:
- Desktop executable output remains `tg`; this is an explicit product requirement, not a review defect.
- `TgCli::Capabilities::ProxyChange` remains a TG-owned DTO to avoid exposing Core::Application types in the capability interface.
- New TG-owned files remain unfenced per policy.

Deferred to relevant injection stage:
- Replace `std::function` callback interfaces with the project's move-only callback types during DomainLifecycleCapabilities injection (A6) per packet 22.
- Evaluate `nice_target_sources` versus raw `target_sources`; this is organization/portability consistency, not an A3 functional blocker.
- CLI no-op behavior for TonSite/theme/window capabilities is implemented and tested in A4-A6, not in the desktop-only A2 forwarders.
