# PLAN_tg_probe_30_a9_existing_account_enumeration
Parent: PLAN_tg_probe_14_core_extraction.md
Previous packet: PLAN_tg_probe_29_a8_1_isolated_profile_snapshot_demo.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Implementation and validation complete; user accepted the account demo; ready for local commit **high**

## Purpose
Implement one hosted console packet that starts the existing Telegram Domain/Account/Session runtime against the dedicated dev profile and enumerates existing authenticated accounts, with deterministic text output and optional JSON output.

Locked decision for A9:
1. Do not implement any new account parser, account storage reader, or duplicate account model.
2. Reuse existing startup path and account containers only:
   - Main::Domain::start(QByteArray())
   - Main::Domain::accounts()
3. Accept normal startup behavior (settings upgrades, writes, network/session startup) only because this packet runs on the dedicated dev profile.
4. Do not claim pure-read behavior or live-profile safety.
5. Call `_domain->start(QByteArray())` directly in the hosted A9 branch. Do not call `Application::startDomain()`: it returns `void` and routes non-success into desktop passcode-lock UI.

## Scope
1. Hosted tg binary mode only; no standalone tg_cli runtime changes.
2. Dedicated profile only: workspace sibling `../tg-dev-profile` (outside the Git repository).
3. No chat/history/send work.
4. No A10 implementation in this packet.

## Out Of Scope
1. Any new account parsing logic.
2. Any new account DTO in Main or Storage modules.
3. Live-profile ownership architecture changes.
4. Any fallback path that creates or resets profile/account state.

## Current Code Baseline (Grounded)
1. Launcher flags currently parsed in core/launcher.cpp include:
   - -console
   - -console-exit
   - -console-log <path>
   - -console-owner-probe
   - -console-profile-snapshot
2. Hosted console startup currently writes console-ready and returns before desktop window/tray startup branch.
3. Core::Application::startDomain() executes Main::Domain::start(QByteArray()) and handles Storage::StartResult by passcode lock flow.
4. Main::Domain::accounts() returns storage-indexed account entries populated via Storage::Domain::startModern() and owner account factory.
5. Storage::Domain::start() can call startFromScratch() when modern startup fails; this must be prevented for invalid profiles in A9.

## A9 Mode Contract (Exact)
Add one dedicated one-shot hosted mode:
1. Flag: -console-accounts
2. Optional argument: -console-account-index <int>
3. Optional argument: -console-format json|text (default text)
4. Required argument: -workdir <path>
5. Required argument: -console-log <path>

Rules:
1. -console-accounts implies -console.
2. -console-accounts is one-shot and always exits after output (no persistent listener mode).
3. -console-profile-snapshot and -console-accounts are mutually exclusive.
4. -console-owner-probe and -console-accounts are mutually exclusive.
5. Any missing required argument is fatal and must exit before storage start.

## Output Contract

### Text output (required)
Write deterministic key:value lines to stdout and hosted status sink:
1. accounts-mode:started
2. accounts-preflight-status:<ready|passcode-required|passcode-required-legacy|profile-corrupt|profile-not-found>
3. accounts-start-result:<success|incorrect-passcode|incorrect-passcode-legacy>
4. accounts-count:<N>
5. accounts-authed-count:<N>
6. accounts-active-storage-index:<index>
7. accounts-selected-storage-index:<index>
8. accounts-row:<storageIndex>|<sessionExists:0|1>|<userId>|<uniqueId>|<env:prod|test>|<username>|<displayName>
9. accounts-mode:done

Token details:
1. storageIndex comes from Main::Domain::AccountWithIndex::index.
2. sessionExists from Main::Account::sessionExists().
3. userId from Main::Session::userId().bare when session exists, else 0.
4. uniqueId from Main::Session::uniqueId() when session exists, else 0.
5. env token from Main::Session::isTestMode() => test, otherwise prod.
6. username from session.user()->username() if available, else empty string.
7. displayName from session.user()->name() if available, else empty string.

Ordering:
1. Rows must follow Main::Domain::accounts() vector order exactly (storage index order as loaded by storage).
2. Do not re-sort in TG code.

### JSON output (optional, practical)
If -console-format json is requested, emit one JSON object line after accounts-mode:started:
1. keys:
   - preflightStatus
   - startResult
   - accountCount
   - authedCount
   - activeStorageIndex
   - selectedStorageIndex
   - accounts (array)
2. each account item:
   - storageIndex
   - sessionExists
   - userId
   - uniqueId
   - environment
   - username
   - displayName

JSON remains experimental and unversioned in A9.

## Readiness Timing And Timeout Contract
Readiness for enumeration is defined as:
1. preflight status is ready, and
2. direct `_domain->start(QByteArray())` returns `Storage::StartResult::Success`, and
3. domain.accounts().size() > 0, and
4. active account exists and is included in accounts().

Timing behavior:
1. Storage-backed account/session identity is expected synchronously after successful `Domain::start()`. Emit rows immediately.
2. Username/displayName are best-effort fields from the already-created self user and may be empty; A9 never waits for network enrichment.
3. If a future asynchronous dependency appears, stop under the packet stop conditions rather than adding polling or an unproven timeout loop.

## Storage::StartResult Handling
1. Success:
   - continue to enumerate and emit records.
2. IncorrectPasscode:
   - emit accounts-start-result:incorrect-passcode
   - emit zero rows
   - do not emit active/selected storage index
   - exit nonzero.
3. IncorrectPasscodeLegacy:
   - emit accounts-start-result:incorrect-passcode-legacy
   - emit zero rows
   - do not emit active/selected storage index
   - exit nonzero.

Forbidden in A9:
1. resetWithForgottenPasscode()
2. startFromScratch() path acceptance for invalid profile
3. any automatic account creation fallback

## Invalid Profile Guard (Prevent Or Detect startFromScratch)
To avoid accidental startFromScratch writes on invalid profile:
1. Preflight guard: call domain.local().classifySnapshotStorage() before startDomain() in A9 mode.
2. If preflight is profile-corrupt or profile-not-found, abort before startDomain().
3. If preflight is passcode-required or passcode-required-legacy, do not start Domain. Emit the matching preflight/start-result token and stop without entering desktop passcode UI.
4. Post-start safety assertion for Success:
   - accounts-authed-count must be > 0.
   - selected account must have sessionExists == 1.
   - if violated, emit accounts-error:unexpected-empty-auth and exit nonzero.

This makes invalid/missing storage fail closed before mutation-capable paths.

## Account Selection Handoff For A10
Selection rule:
1. Default selected index is domain.activeForStorage().
2. If -console-account-index is supplied, it must match an existing accounts() storage index.
3. If supplied index not found, emit accounts-error:invalid-account-index and exit nonzero.

Handoff artifact:
1. Emit accounts-selected-storage-index:<index>.
2. A10 consumes this storage index as its account selector, without re-deriving ordering.

## No-Window Lifecycle And Clean Shutdown
A9 lifecycle rules:
1. No Window::Controller creation.
2. No tray startup.
3. No media overlay startup.

Shutdown rule to avoid half-init crash:
1. Do not call std::_Exit in A9 success/failure path after Domain start.
2. Exit through normal Qt/application shutdown path after emission:
   - request Quit()
   - call QCoreApplication::exit(<code>) only after event loop exists
3. Because `Application::run()` executes before the event loop starts, schedule final output/quit with `crl::on_main` (or equivalent established main-loop scheduling) and return from `run()`; do not call `QCoreApplication::exit()` synchronously before `exec()`.
4. Keep profile-snapshot mode behavior unchanged (its current `_Exit` path remains separate).

Rationale:
1. Profile-snapshot exits from intentionally half-initialized state.
2. A9 initializes Domain/Account/Session; clean teardown should run destructors and Domain::finish().

## Protected Files And Fence IDs
Existing upstream-owned files expected to change:
1. Telegram/SourceFiles/settings.h
   - core-console-accounts-launch-state
2. Telegram/SourceFiles/settings.cpp
   - core-console-accounts-launch-state
3. Telegram/SourceFiles/core/launcher.cpp
   - launcher-console-accounts-argument-parse
   - launcher-console-accounts-flag-assign
   - launcher-console-accounts-gates
4. Telegram/SourceFiles/core/application.cpp
   - application-console-accounts-include
   - application-console-accounts-branch
   - application-console-accounts-readiness
   - application-console-accounts-clean-exit

No fenced changes planned in Main/Domain/Storage for A9 unless a blocker forces a narrow additive seam.

## TG-Owned Files Planned
1. Telegram/tg_cli/hosted/hosted_console_accounts_mode.h
2. Telegram/tg_cli/hosted/hosted_console_accounts_mode.cpp
3. Telegram/tg_cli/hosted/hosted_console_accounts_format.h
4. Telegram/tg_cli/hosted/hosted_console_accounts_format.cpp
5. Telegram/tg_cli/tools/test_hosted_console_accounts_packet30.ps1

Optional TG-owned file if JSON writer extracted:
1. Telegram/tg_cli/hosted/hosted_console_json_writer.h
2. Telegram/tg_cli/hosted/hosted_console_json_writer.cpp

## Ordered Implementation Plan
1. Add launcher argument parsing and launch-state flags for -console-accounts, -console-account-index, -console-format.
A9q. [DONE] Launcher A9 gates use a sibling top-level fence; packet-26 fences remain independently auditable.
A9r. [DONE] Apparent build instability was caused by overlapping/orphaned multiprocess compiler runs. Stable validation uses one MSBuild node plus compiler-level `_CL_=/MP1`; a fresh Telegram binary linked and all runtime tests passed.
2. Add hosted A9 mode orchestrator in TG-owned hosted files.
3. Add Application A9 branch that:
   - validates mode arguments,
   - performs snapshot-status preflight,
   - directly runs `_domain->start(QByteArray())`,
   - handles StartResult,
   - enumerates domain.accounts(),
   - writes deterministic output,
   - exits cleanly.
4. Keep existing -console checkpoint behavior unchanged when -console-accounts is absent.
5. Keep -console-profile-snapshot branch unchanged.

## Validation Plan
1. Build checks:
   - cmake --build out --config Debug --target Telegram
   - cmake --build out --config Debug --target tg_cli
2. Fence and diff checks:
   - python Telegram/tg_cli/tools/check_tg_change_fences.py --repo . --base 12e8d4a956
   - git diff --check
3. Runtime checks (dedicated dev profile only):
   - tg.exe -console-accounts -workdir <devprofile> -console-log <outside>
   - repeat run and compare account rows and selected index.
   - tg.exe -console-accounts -console-format json ... and verify JSON keys.
4. Negative checks:
   - missing -workdir => nonzero, no startup.
   - missing -console-log => nonzero, no startup.
   - invalid -console-account-index => nonzero.
   - profile-not-found/profile-corrupt preflight => nonzero before startDomain.
   - passcode-required profile => explicit passcode token and nonzero.
5. Regression checks:
   - packet-26 hosted checkpoint script still passes.
   - normal non-console tg startup unchanged.

## Definition Of Done
1. One focused A9 implementation commit exists.
2. Uses existing Application startDomain and Domain accounts APIs only.
3. No duplicate account parser/model introduced.
4. Deterministic text output implemented; JSON optional and functional when enabled.
5. Selected account storage index output provided for A10 handoff.
6. Clean no-window lifecycle with safe termination, no half-init crash path.
7. Invalid profile preflight prevents mutation-prone startup path.
8. All validations pass: Telegram build, tg_cli build/help, packet-26 regression, fence check, git diff --check.

## Stop Conditions
1. A9 requires creating a new account parser/model.
2. Domain accounts cannot be populated without constructing any window/controller.
3. Hosted capability no-op setup prevents session creation on valid dev profile.
4. startFromScratch cannot be prevented/detected for invalid profile with bounded additive changes.
5. Clean teardown repeatedly crashes or hangs after successful Domain start.

If any stop condition is hit:
1. stop implementation,
2. capture evidence,
3. open a bounded follow-up packet before touching A10.

## Rollback Plan
If A9 implementation regresses hosted checkpoint or desktop startup:
1. Revert only packet-30 implementation commit(s).
2. Re-run:
   - Telegram Debug build
   - tg_cli Debug build/help
   - packet-26 hosted checkpoint regression script
   - fence checker and git diff --check
3. Keep packet 30 plan file and evidence notes; do not carry partial runtime behavior.

## Unresolved Blockers (Pre-Implementation)
1. None.

## Completion Evidence
1. Telegram Debug build: PASS with one MSBuild node and `_CL_=/MP1`; fresh `out/Debug/tg.exe` linked.
2. tg_cli Debug build and `--help`: PASS.
3. Packet-30 runtime matrix: `HOSTED_CONSOLE_ACCOUNTS_PACKET30_TESTS=PASS`.
4. Persistent dev-profile path: workspace sibling `C:/work/git/tdesktop/tg-dev-profile` (outside Git). After one login/2FA at the new path, real output exits 0 with preflight `ready`, start result `success`, account/authed count 1, active/selected storage index 0, and one production account row with user/unique id `3527271`, username `vmalkov`, and UTF-8 display name `Владимир`.
5. Determinism: two text runs have identical account rows and selected index; JSON contains all required keys. JSON keeps 64-bit ids as decimal strings to avoid IEEE-754 precision loss.
6. Negative tests: missing workdir, missing console log, invalid account index, empty profile, and mutually exclusive mode combination all return expected nonzero outcomes; invalid/missing profiles stop before Domain startup.
7. Packet-26 hosted console regression: PASS all four stages, no leaked process.
8. Normal desktop disposable-workdir startup and same-workdir quit: PASS; both exit 0.
9. Fence checker self-test/full branch: PASS. `git diff --check`: PASS.
10. Independent reviews: initial review found preflight ordering/text escaping/test-harness defects; subsequent reviews found duplicate error status fields and a multi-account active-index edge case. All findings were fixed and the matrix rerun PASS.
11. User acceptance: PASS. User ran `run_hosted_console_accounts_demo.ps1`, verified the real account row, and requested the A10/interactive roadmap be locked in writing. Local commit closes A9; A10 has not started.
12. Persistent-path validation exposed two latent hosted lifecycle differences from desktop: hosted `runOnMain()` executed callbacks synchronously, allowing account removal/write before startup settled; and a headless account without a window was treated as redundant before its asynchronous session arrived. Hosted behavior now preserves desktop main-loop deferral, keeps sessionless accounts during bounded headless startup, waits up to 15 seconds on existing `Account::sessionValue()` producers, and selects the active authenticated account or first authenticated fallback. Desktop capability behavior remains unchanged (`keepAccountWithoutSession=false`).
