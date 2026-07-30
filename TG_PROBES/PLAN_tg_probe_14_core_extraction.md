# PLAN_tg_probe_14_core_extraction
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_fallback_architectures.md
Status: [TODO] A0/A1/A2 committed; A0.1/A2.1 corrections and desktop validation required before A3 **high**

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
Main::Domain receives an additive constructor overload accepting a non-owning bundle of the four capabilities. The existing `Domain(const QString&)` remains and delegates to desktop capabilities, preserving existing call sites.

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
Status: [PARTIAL] Interfaces/desktop forwarders and fenced CMake wiring are committed. Desktop behavior is not yet validated; prior logs show both PDB update failures and later disk exhaustion.
1. Add TG-owned capability interface files.
2. Add desktop forwarding implementations.
3. Wire files into the desktop build inside fenced CMake blocks only.
4. Do not inject them into Main/Storage yet.

Pass:
- Desktop Debug builds/runs with no behavior change.
- Fence checker passes.

### A2.1: Capability Baseline Corrections
Status: [TODO] Must pass before A3.
Implementor packet: PLAN_tg_probe_19_a2_1_baseline_corrections.md
1. Include `crl::time` from its direct declaration header.
2. Document intentional TG-owned ProxyChange DTO boundary.
3. Confirm no tg_cli path calls desktop factories that require Core::App().
4. Validate disk/process preconditions before desktop build.
5. Complete desktop Debug build and startup smoke test.

### A3: Main::Account Injection
Implementor packet: PLAN_tg_probe_17_a3_account_network_injection.md
1. Add an overload/accessor while preserving current constructor.
2. Replace only Main::Account network/config globals with AccountNetworkCapabilities.
3. Existing desktop constructor selects desktop capabilities.

Pass:
- Desktop proxy/config behavior smoke test passes.
- tg_cli probe can construct the account-network capability object.

### A4: Storage::Account Injection
1. Pass StorageSettingsCapabilities transitively from Main::Account.
2. Replace theme/token/settings globals.
3. Preserve existing constructor behavior.

Pass:
- Desktop profile opens and theme/token settings round-trip.
- No CLI test writes desktop tdata.

### A5: Main::Session Injection
1. Pass SessionServiceCapabilities transitively from Main::Account.
2. Replace email-lock/download/window global calls.
3. Require null-safe optional window behavior.

Pass:
- Desktop session startup and window selection remain correct.
- CLI construction creates no windows.

### A6: Main::Domain Injection
1. Add capability-bundle constructor overload; preserve `Domain(const QString&)`.
2. Convert required lifecycle calls first.
3. Convert optional presentation hooks one cluster at a time.
4. Add a fenced Main::Domain account factory that selects capability implementations.
5. Change Storage::Domain account construction to call the owner factory; preserve desktop output and account ordering.

Pass:
- Desktop multi-account activation/window behavior passes.
- CLI can start Domain without notifications/windows/export prompts.

### A7: Account/Session Construction Probe
1. Use TG-owned capabilities and synthetic temporary workdir.
2. Construct Domain/Account without real profile access.
3. Determine QCoreApplication versus QApplication requirement.

Pass:
- Clean construction/teardown, no windows, no profile mutation.

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
- Replace `std::function` callback interfaces with the project's move-only callback types before DomainLifecycleCapabilities injection (A6), if signatures can remain dependency-cohesive.
- Evaluate `nice_target_sources` versus raw `target_sources`; this is organization/portability consistency, not an A3 functional blocker.
- CLI no-op behavior for TonSite/theme/window capabilities is implemented and tested in A4-A6, not in the desktop-only A2 forwarders.
