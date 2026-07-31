# PLAN_tg_probe_26_a7_3_hosted_console_mode_checkpoint
Parent: PLAN_tg_probe_14_core_extraction.md
Previous packet: PLAN_tg_probe_25_backend_architecture_decision.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] A7.3 hosted console checkpoint implemented and validated; 8q resolved as Option B (no hosted Domain start on disposable empty workdir); A8 may open **high**

## Purpose
Define the smallest executable hosted-console checkpoint for Option C selected by packet 25, using the existing fully linked `tg` runtime path (`Launcher -> Sandbox -> Application -> Domain`) with deterministic UI suppression and no standalone `tg_cli` runtime integration.

This packet is checkpoint-only by design:
1. Add `-console` mode parsing with no conflict.
2. Make mode state available before `Main::Domain` construction.
3. Select a hosted capability bundle for `Main::Domain` without linking it into standalone `tg_cli`.
4. Suppress initial/activation windows at the narrowest existing points.
5. Emit one console-ready/status output line and optionally exit cleanly.
6. Prove same-workdir second instance rejection/forward behavior.
7. Preserve default desktop behavior when `-console` is absent.

## Roadmap Pointer Update
Packet 25 declared this packet as the immediate next implementor packet for the hosted path. The roadmap pointer for A7.3 is now this file.

## Locked Scope And Non-Goals
1. No `lib_*` edits.
2. No packaging redesign, no subsystem switch, no broad logging redesign.
3. No interactive command loop.
4. Synthetic empty `-workdir` only for packet validation; allowed writes are limited to that workdir's disposable `tdata` and explicit hosted status output file.
5. Real/default desktop profile paths are forbidden for this packet, including implicit default path resolution.
6. Keep standalone `Telegram/tg_cli/main.cpp` and current `tg_cli` skeleton behavior unchanged.
7. No A8 profile probe activity in this packet.

## Read-Only Code Path Baseline (Current)

### 1. Startup chain and argument parsing
1. `Telegram/SourceFiles/main.cpp`
   - `main()` calls `Core::Launcher::Create(argc, argv)` then `launcher->exec()`.
2. `Telegram/SourceFiles/core/launcher.cpp`
   - `Launcher::processArguments()` parses known switches via `parseMap`.
   - Unknown switches are currently captured under `"--"` and become `gStartUrls` candidates.
   - `Launcher::exec()` starts logs, initializes platform, then `executeApplication()`.
   - `executeApplication()` creates `Sandbox` and calls `sandbox.start()`.

### 2. Workdir/single-instance ownership path
1. `Telegram/SourceFiles/logs.cpp`
   - `Logs::start()` calls `launcher.checkPortableVersionFolder()`, resolves default workdir, applies `launcher.validateCustomWorkingDir()`, then creates `tdata` and logs command line.
2. `Telegram/SourceFiles/core/sandbox.cpp`
   - `Sandbox::start()` computes `_localServerName` from hash of effective `cWorkingDir()` absolute path.
   - Connects `_localSocket` to existing primary instance.
   - If connected, `socketConnected()` marks `_secondInstance=true`, sends commands, and quits on response.
   - If no server, `socketError(ServerNotFoundError)` starts listening and calls `singleInstanceChecked()`.

### 3. Domain creation and UI creation points
1. `Telegram/SourceFiles/core/sandbox.cpp`
   - `launchApplication()` constructs `Core::Application` and calls `_application->run()`.
2. `Telegram/SourceFiles/core/application.cpp`
   - `Application` constructor currently creates `_domain` as `std::make_unique<Main::Domain>(cDataFile())`.
   - `run()` creates a `Window::Controller` in `_windows`, calls `startDomain()`, `startTray()`, `firstShow()`, `startMediaView()`, `finishFirstShow()`, and activation-related startup.
3. `Telegram/SourceFiles/core/sandbox.cpp`
   - `execExternal("show")` activates `Core::App().activePrimaryWindow()` or prelaunch window.

### 4. A3-A6 capability selection APIs already present
1. `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h/.cpp`
   - `DomainCapabilityBundle`, `DomainAccountFactoryCapabilities`, `CreateDesktopDomainCapabilityBundle()` exist.
2. `Telegram/SourceFiles/main/main_domain.h/.cpp`
   - `Domain(const QString&)` delegates to bundle overload.
   - Bundle stores lifecycle + account-factory capabilities.
   - `createAccountForStorage()` uses account factory bundle.
3. `Telegram/SourceFiles/main/main_account.cpp`
   - Default ctor chain selects desktop `AccountNetwork/StorageSettings/SessionService` capability creators.
4. `Telegram/tg_cli/capabilities/session_service_capabilities_cli.cpp` and `storage_settings_capabilities_cli.cpp`
   - Null/no-op CLI behavior already exists and is suitable for hosted-console windowless operation.

## Hosted Console Checkpoint Design

### 1. Mode and output contract
1. New switch: `-console`.
2. Optional switch: `-console-exit` (clean early exit after status line).
3. Optional switch: `-console-log <path>` (explicit writable file path).
4. Default output channel policy on Windows GUI subsystem:
   - Primary deterministic channel: append status lines to synthetic-workdir log file (`<workdir>/tdata/console_bootstrap.log`).
   - If `-console-log` is provided, use that path instead.
   - If stdout is attached (developer-launched console), mirror line to stdout best-effort.

Rationale: GUI subsystem stdout visibility is not guaranteed. File output under synthetic workdir is testable without packaging changes.

### 2. Earliest mode-state availability requirement
1. Parse `-console` in `Launcher::processArguments()` to avoid unknown-argument fallback into URL parsing.
2. Expose mode state through a narrow runtime flag API in Core globals (same layer as other launch flags), readable before `Application` constructor creates `Main::Domain`.
3. No behavior change when `-console` is absent.

### 3. Hosted capability bundle selection (without standalone tg_cli binding)
1. Add hosted-only bundle factory in `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.cpp`:
   - `CreateHostedConsoleDomainCapabilityBundle()`.
2. Bundle shape:
   - Lifecycle capability forwards required runtime ownership calls through `Core::Application` (`fallbackProductionConfig`, settings persistence) but no-ops presentation/window hooks.
   - Account factory creates:
     - hosted network capability using shared fallback config from `Core::Application`.
     - existing CLI no-op `SessionServiceCapabilities`.
     - existing CLI no-op `StorageSettingsCapabilities`.
3. Compile hosted capability implementation into `Telegram` target only.
4. Do not link/route hosted bundle into standalone `tg_cli` target.
5. Keep existing desktop capability factory behavior untouched (`CreateDesktopDomainCapabilityBundle()` remains the default desktop path with no behavior changes).
6. Hosted bundle implementation lives in a separate TG-owned source file under `Telegram/tg_cli/hosted/`, wired into `Telegram` target only.

### 4. Deterministic window suppression at narrow points
1. Primary suppression point in `Application::run()`:
   - Skip initial primary window allocation and all first-show/startTray/startMediaView/start-settings UI paths when hosted console mode is on.
2. Activation suppression in `Sandbox::socketConnected()` and `readClients()`:
   - In console mode, avoid sending/processing activation (`CMD:show`) commands for second instance; use a console-specific command (`CMD:console`) or empty command path that still yields `RES:` response and quit.
   - In console-mode primary, `execExternal("show")` remains no-op/0 window id semantics.
3. Prelaunch and crash UI:
   - If crash-report path currently creates prelaunch windows (`NotStartedWindow`, `LastCrashedWindow`), hosted mode must take fail-closed path: emit status line and exit nonzero rather than opening UI.

### 5. Exact bounded startup-retained operations for hosted checkpoint
Inspected baseline sequence (`Application::run()`) is constrained as follows.

1. Retain only operations required for hosted owner/event-loop startup:
   - `_notifications` initialization.
   - `startLocalStorage()`.
   - `refreshGlobalProxy()`.
   - translator installation (`QCoreApplication::installTranslator`).
   - deterministic hosted status line output.
2. Explicitly skip hosted-window/UI branch operations:
   - `_windows.emplace(...)` primary window creation.
   - active-account `showAccount(...)` subscription wiring that assumes window presence.
   - `startTray()`, `firstShow()`, `startMediaView()`, `finishFirstShow()`, `showSettings()`.
   - startup OpenGL crash notification UI.
3. Domain creation capability bundle selection remains compiled and selected in `Application` constructor, but authenticated `Main::Domain::start(...)` is deferred to A8.

### 6. Single-instance and synthetic workdir behavior
1. Enforcement remains existing server-name hash by `cWorkingDir()` in `Sandbox::start()`.
2. Validation requires two hosted instances with identical synthetic workdir:
   - First becomes owner/listener.
   - Second must connect, receive deterministic response, and exit without taking ownership.
3. No real desktop profile path usage during packet validation.

## Proposed APIs (Additive)

### 1. Core launch state API
1. Add additive launch flags in existing launch-state storage module used by `cQuit/cStartInTray/...`:
   - `bool cConsoleMode();`
   - `void cSetConsoleMode(bool);`
   - `bool cConsoleExitRequested();`
   - `void cSetConsoleExitRequested(bool);`
   - `QString cConsoleLogPath();`
   - `void cSetConsoleLogPath(const QString &);`

### 2. Hosted capability factory API
1. In `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h`:
   - declare `DomainCapabilityBundle CreateHostedConsoleDomainCapabilityBundle();`

### 3. Console status output helper
1. New small TG-owned utility under `Telegram/tg_cli/hosted/` compiled into `Telegram` only:
   - `void WriteHostedConsoleStatusLine(const QString &line);`
2. Writes to selected file sink and mirrors to stdout if attached.
3. Do not add new implementation files under `Telegram/SourceFiles/core` for hosted status output.

## Fence Plan (IDs)
All protected-source edits must use these fence IDs at minimum cohesive scope.

1. `launcher-console-argument-parse`
2. `launcher-console-flag-assign`
3. `core-console-launch-state`
4. `application-console-domain-bundle-select`
5. `application-console-window-suppress`
6. `application-console-status-line`
7. `application-console-optional-exit`
8. `sandbox-console-second-instance-command`
9. `sandbox-console-exec-external-suppress`
10. `domain-hosted-console-bundle-factory`
11. `telegram-cmake-hosted-console-capability-sources`
12. `telegram-cmake-hosted-console-status-writer-sources`

If code locality requires split blocks, suffix with `-a`, `-b` while keeping semantic ID root stable.

## Ordered Edit Plan (Smallest Defensible Diff)

1. Add launch-flag storage for hosted console state and log path (no behavior yet).
2. Extend `Launcher::processArguments()` parse map to include `-console`, `-console-exit`, `-console-log` and assign flags.
3. Add TG-owned hosted status writer utility under `Telegram/tg_cli/hosted/` with file-first sink policy.
4. Add hosted capability bundle factory in separate TG-owned hosted source; wire it into `Telegram` target only while keeping desktop factory behavior unchanged.
5. In `Application` construction path, select `Main::Domain` bundle based on `cConsoleMode()`.
6. [DONE] Stage Gate H1 (must pass before persistent owner mode): implement reversible one-shot hosted path for `-console -console-exit`.
   Outcome: PASS on synthetic workdir (`%TEMP%\tg_probe26_h1_03`): exit code 0, exactly one status line (`console-ready`), no persistent owner retained.
7. In `Application::run()`, add hosted path:
   - initialize only runtime pieces needed for owner/event-loop startup.
   - skip window/tray/media initialization.
   - emit one deterministic `console-ready` line.
   - honor optional `-console-exit` by clean `Quit()`/`QCoreApplication::exit(0)`.
8. [DONE] Stage Gate H2: in `Sandbox` second-instance path, avoid `CMD:show` behavior for console mode and keep deterministic rejection/forward response.
   Outcome: PASS on synthetic same-workdir probe (`%TEMP%\tg_probe26_h2_optionb_rerun`): first process stayed owner/listener; second process exited via `RES` path with `windowId=0`.

8q. [DONE] Option B selected and implemented (high) - Hosted persistent-owner mode on synthetic empty workdir skips `Main::Domain` startup and remains IPC-listener only.
   Outcome: assertion path (`storage_domain.cpp:214`) no longer reproduces in hosted probes; no `Storage::Domain` invariant edits were made.
9. CMake wiring: compile hosted capability and hosted status source(s) into `Telegram` target only, leave `tg_cli` target untouched.

## Validation Plan

### 1. Static and fence checks
1. Run TG fence checker against branch base (same baseline used in prior packets).
2. Run `git diff --check`.
3. Confirm only intended files changed; no `lib_*` modifications.

### 2. Build checks
1. Build `Telegram` Debug target.
2. Build `tg_cli` target and run `tg_cli --help` to confirm skeleton unchanged.
3. Confirm no unfenced edits in existing upstream CMake/source files touched by this packet.

### 3. Hosted runtime checks (synthetic workdir only)
1. Gate H1 (one-shot only):
   - `tg.exe -console -console-exit -workdir <syntheticProbe>`.
   - verify clean exit after exactly one deterministic status line.
   - verify no startup desktop window and no ownership/listener persistence.
2. Launch persistent owner mode only after H1 pass:
   - `tg.exe -console -workdir <syntheticA>`.
   - verify no initial desktop window appears.
   - verify status line persisted to `<syntheticA>/tdata/console_bootstrap.log` or explicit `-console-log` path.
3. Optional exit (repeatability):
   - `tg.exe -console -console-exit -workdir <syntheticB>` exits cleanly after status line.
4. Same-workdir second-instance behavior:
   - Start first hosted instance with `<syntheticC>`.
   - Start second hosted instance with same workdir.
   - verify second exits via existing IPC response path and does not acquire ownership concurrently.
5. Regression check:
   - launch normal `tg.exe` without `-console` and verify standard desktop startup unchanged.
6. Forbidden-path check:
   - do not run hosted validation on default/real profile paths.

## Pass Criteria For This Packet
1. `-console` is parsed without conflicting with URL/`--` argument handling.
2. Hosted mode state is available before `Main::Domain` construction.
3. Hosted capability bundle is selected in `Telegram` runtime, not standalone `tg_cli`.
4. No startup/activation windows in hosted mode at identified narrow points.
5. One deterministic console-ready/status output line is produced.
6. Optional clean exit flag works.
7. Same synthetic workdir second instance is rejected/forwarded via existing IPC ownership model.
8. Desktop behavior without `-console` is unchanged.
9. A8 remains CLOSED unless all above pass.

## Rollback / Stop Conditions
1. If hosted mode requires invasive refactor across unrelated UI subsystems, stop and split into a narrower sub-packet.
2. If window suppression cannot be done at `Application::run` and `Sandbox` command points without regressions, stop and escalate with evidence.
3. If same-workdir single-instance behavior weakens (concurrent ownership), revert packet edits and stop.
4. If fallback config sharing cannot be kept via `Core::Application` in hosted mode, stop before A8.
5. If any solution requires modifying standalone `tg_cli` runtime behavior, stop (out of scope).
6. If H1 one-shot probe is not clean (window shown, ambiguous status, or non-deterministic exit), stop before H2 persistent owner/single-instance edits.

## Explicit A8 Gate
A8 was CLOSED by default. This packet now records pass criteria and validation evidence, so A8 may open.

## Expected Hand-off To A8 (Only If Packet Passes)
1. Proven hosted runtime boot in synthetic workdir with no window creation.
2. Proven single-owner semantics for same-workdir instances.
3. Proven hosted capability bundle selection path and fallback config ownership via `Core::Application`.
4. Stable status output channel for CI/manual probes.

## Validation Evidence (2026-07-31)
1. Fence/static checks:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo . --base 12e8d4a956` -> PASS.
   - `git diff --check` -> PASS.
2. Builds:
   - `cmake --build out --config Debug --target Telegram` -> PASS.
   - `cmake --build out --config Debug --target tg_cli` -> PASS.
   - `out/Telegram/tg_cli/Debug/tg_cli.exe --help` -> PASS.
3. Hosted runtime checks (synthetic disposable workdirs only):
   - H1 one-shot `-console -console-exit` -> exit code 0; `console-ready` persisted (default log and explicit `-console-log` both confirmed by raw file bytes).
   - H2 persistent owner `-console` -> owner remains alive; second same-workdir process exits with response path (`windowId=0`) and does not take ownership.
   - Hosted startup log contains no window creation milestones.
4. Non-console regression:
   - normal `tg.exe -workdir <synthetic>`, followed by second `-quit` handshake -> both exits 0; owner did not exit early.

## Known Blockers / Risks To Track During Implementation
1. Windows GUI stdout invisibility: must rely on file sink for deterministic verification.
2. Crash/prelaunch branches currently create windows; hosted mode may need explicit fail-closed no-UI branch.
3. Existing single-instance protocol is show/quit oriented; console command semantics must remain backward-safe.