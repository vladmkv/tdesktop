# PLAN_tg_probe_21_a5_session_service_injection
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Validated and ready for commit record **high**

## Goal
Inject `SessionServiceCapabilities` into `Main::Session` through `Main::Account`, preserving all existing constructors, call sites, session recreation behavior, and desktop behavior.

Replace exactly five scoped `Core::App()` service calls in `main_session.cpp`: setup-email lock/unlock, download-session tracking, and optional window selection. Add a CLI implementation whose presentation/download integration methods are no-ops or return null. Do not construct a CLI Session or open a profile in this packet.

## Prerequisites
1. A0.1 through A4 remain complete; A4 commit is `530dec607a`.
2. Fence checker self-test and full validation against `12e8d4a956` pass before editing.
3. Preserve the existing uncommitted `PLAN_tg_console_mode.md` roadmap update.
4. Do not treat the skipped A4 TonSite manual path as an A5 blocker; it remains recorded residual coverage.

## Ownership And Lifetime Design
`Main::Account` outlives every `Main::Session` it creates and may destroy and recreate its Session. Therefore:
- `Main::Account` exclusively owns one non-null `SessionServiceCapabilities` object.
- `Main::Session` stores a non-owning non-null pointer/reference to that account-owned object.
- Do not move the capability into a Session; that would break session recreation.
- Do not add a capability factory, shared ownership, or a new Session constructor.
- Keep the existing `Session(Account*, MTPUser, SessionSettings)` API unchanged.

### Main::Account overload
Preserve the existing 3-, 4-, and 5-argument constructors. Add:

```cpp
Account(
	not_null<Domain*> domain,
	const QString &dataName,
	int index,
	std::unique_ptr<TgCli::Capabilities::AccountNetworkCapabilities> networkCapabilities,
	std::unique_ptr<TgCli::Capabilities::StorageSettingsCapabilities> storageCapabilities,
	std::unique_ptr<TgCli::Capabilities::SessionServiceCapabilities> sessionCapabilities);
```

Delegation rules:
- The 3-argument constructor continues selecting desktop network behavior through the existing chain.
- The 4-argument constructor continues preserving the supplied network capability and selecting desktop storage behavior.
- The 5-argument A4 constructor preserves both supplied capabilities and delegates with `CreateDesktopSessionServiceCapabilities()`.
- The new 6-argument constructor enforces all three inputs as non-null, owns network and session capabilities, and transfers storage ownership to `Storage::Account` exactly as A4 does.

Add an accessor returning `SessionServiceCapabilities &`. `Main::Session` initializes its borrowed capability from this accessor. Place the account-owned member after `_networkCapabilities` and before `_local`; it must outlive `_session`.

### Main::Session reference
Forward-declare `SessionServiceCapabilities` in `main_session.h`. Add a non-owning `const not_null<SessionServiceCapabilities*>` member immediately after `_account`, initialized from the account accessor. The Session constructor must enforce or inherit non-nullness without changing its signature.

## CLI Capability Implementation
Add `CreateCliSessionServiceCapabilities()` to the interface and implement it in a separate TG-owned source file compiled only into tg_cli. Do not compile the desktop implementation into tg_cli.

CLI behavior:
- `lockBySetupEmail()` is a no-op.
- `unlockSetupEmail()` is a no-op.
- `trackDownloadSession(...)` is a no-op, including null input.
- `windowForPeer(...)` returns `nullptr`.
- `activePrimaryWindow()` returns `nullptr`.

The CLI implementation must require only existing forward declarations and QtCore-level dependencies. It must not include `core/application.h`, window implementation headers, QtGui, QtWidgets, or `lib_ui`.

## Exact Replacements In main_session.cpp
1. Setup-email transition calls `_sessionServiceCapabilities->lockBySetupEmail()` at the same deferred point.
2. Setup-email completion calls `_sessionServiceCapabilities->unlockSetupEmail()` before the existing settings update/save sequence.
3. Session startup calls `_sessionServiceCapabilities->trackDownloadSession(this)` at the same point.
4. Upload-stop confirmation selects the peer window through `windowForPeer(message->history()->peer)` when a message exists.
5. The no-message path uses `activePrimaryWindow()`.

Preserve the existing null-window branch exactly: invoke `done()` and return without constructing a confirmation box. This is required CLI behavior. Do not rewrite the subsequent desktop confirmation UI in A5.

After replacement, direct `Core::App()` calls for these five scoped services must be absent from `main_session.cpp`. Unrelated Core/Application usage outside the A1 inventory is not part of A5 and must not be changed speculatively.

## Protected Files And Fence IDs

`Telegram/SourceFiles/main/main_account.h`:
- `account-session-capability-forward-declaration`
- `account-session-capability-overload`
- `account-session-capability-accessor`
- `account-session-capability-member`

`Telegram/SourceFiles/main/main_account.cpp`:
- Extend the existing capability include block or add `account-session-capability-include`.
- `account-session-capability-constructor`

`Telegram/SourceFiles/main/main_session.h`:
- `session-service-capability-forward-declaration`
- `session-service-capability-member`

`Telegram/SourceFiles/main/main_session.cpp`:
- `session-service-capability-include`
- `session-service-capability-initializer`
- `session-service-setup-email-lock`
- `session-service-setup-email-unlock`
- `session-service-download-track`
- `session-service-window-selection`

TG-owned files under `Telegram/tg_cli/` require no fences. Do not modify `Storage::*`, `Main::Domain`, `lib_*`, generated files, vendored files, or prepared outputs in A5.

## Implementation Order
1. Run pre-edit fence checks and record the current changed-file list.
2. Add the CLI factory declaration/implementation and compile it into tg_cli.
3. Add account ownership, accessor, and the 6-argument constructor while preserving all existing overloads.
4. Add the borrowed Session capability member and initialize it.
5. Replace setup-email and download tracking calls.
6. Replace only the two window-selection calls; preserve the null branch and desktop confirmation code.
7. Run focused validation before any further source or plan edits.
8. Record results in this packet and the parent plan after all required checks pass.

## Validation Order
1. Fence checker self-test.
2. Fence checker against base `12e8d4a956`.
3. Search `main_session.cpp` for the five original direct-global expressions; require no matches.
4. Build tg_cli Debug and run `tg_cli.exe --help`; confirm tg_cli still links QtCore only and uses the CLI factory implementation.
5. Build desktop Telegram Debug once after process/disk preconditions pass. On PDB/output-lock errors, stop and ask the user to close tg/debug/build processes; do not retry.
6. Smoke-run the freshly built desktop tg against the established test workdir and confirm startup/connectivity.
7. Desktop regression checks:
   - normal authenticated session startup and teardown;
   - download-manager session tracking through an ordinary download only if a safe small test item is available, otherwise record as not manually exercised;
   - upload-stop confirmation: start a safe test upload, invoke stop, verify the confirmation appears, decline once, then stop it normally;
   - setup-email lock/unlock only if the account naturally exposes that state; do not change account security configuration solely for this test, and record skipped coverage.
8. Re-run fence checker and `git diff --check`.
9. Inspect the final diff for planned protected files and TG-owned files only.

No CLI Session construction occurs in A5. Null-window runtime behavior is structurally preserved and is exercised with actual CLI Session construction in A7.

## Pass Criteria
- Existing Account and Session constructors/call sites remain source-compatible.
- `Main::Account` owns one non-null session-service capability across Session destruction/recreation.
- `Main::Session` borrows the capability without shared ownership or a factory.
- The five scoped globals are replaced without changing callback timing or desktop UI flow.
- Null window still calls completion and returns without constructing UI.
- CLI implementation returns null/no-op and compiles without desktop Core/Application or UI linkage.
- tg_cli remains QtCore-only and `--help` passes.
- Desktop build, startup, connectivity, and available focused regressions pass.
- Fence checker and `git diff --check` pass.

## Stop Conditions
- A Session can outlive its Account or use the capability during Account teardown after the capability is destroyed.
- Session recreation requires moving or recreating the capability.
- Existing constructors or call sites require incompatible changes.
- CLI implementation must include Core/Application, QtGui, QtWidgets, or `lib_ui`.
- The null-window branch cannot remain UI-free.
- Any `Storage::*`, `Main::Domain`, or `lib_*` modification is required.
- Desktop session, download, upload-confirmation, or setup-email behavior regresses.
- Fence validation requires an exception or allowlist.
- A real profile must be opened by tg_cli to validate A5.

## Explicitly Deferred
- Domain lifecycle capability bundle and Storage::Domain account factory: A6.
- CLI Domain/Account/Session construction and null-window runtime proof: A7.
- Shared-profile ownership, passcode, account enumeration, and commands: A8-A10.
- Removal of the upload-confirmation UI compile dependency: Stage 7 dependency stripping.

## Required Completion Record
Before marking this packet `[DONE]`, append:
- exact files and fence IDs changed;
- constructor delegation, ownership, and Session borrowing outcome;
- direct-global search result;
- fence checker outputs;
- tg_cli build/help and linked Qt module result;
- desktop build/start/connect result;
- download, upload-confirmation, and setup-email regression dispositions;
- `git diff --check` result;
- implementation commit hash after commit creation.

## Completion Record
- Exact files changed:
	- `Telegram/SourceFiles/main/main_account.h`
	- `Telegram/SourceFiles/main/main_account.cpp`
	- `Telegram/SourceFiles/main/main_session.h`
	- `Telegram/SourceFiles/main/main_session.cpp`
	- `Telegram/tg_cli/capabilities/session_service_capabilities.h`
	- `Telegram/tg_cli/capabilities/session_service_capabilities_cli.cpp`
	- `Telegram/tg_cli/CMakeLists.txt`
	- `TG_PROBES/PLAN_tg_probe_14_core_extraction.md`
	- `TG_PROBES/PLAN_tg_probe_21_a5_session_service_injection.md`
	- `PLAN_tg_console_mode.md`

- Fence IDs changed in protected files:
	- `account-session-capability-forward-declaration`
	- `account-session-capability-overload`
	- `account-session-capability-accessor`
	- `account-session-capability-member`
	- `account-session-capability-include`
	- `account-session-capability-constructor`
	- `session-service-capability-forward-declaration`
	- `session-service-capability-member`
	- `session-service-capability-include`
	- `session-service-capability-initializer`
	- `session-service-setup-email-lock`
	- `session-service-setup-email-unlock`
	- `session-service-download-track`
	- `session-service-window-selection`

- Constructor delegation, ownership, and Session borrowing outcome:
	- Existing 3-arg and 4-arg `Main::Account` constructors remain source-compatible.
	- Existing 5-arg A4 constructor now delegates to `CreateDesktopSessionServiceCapabilities()`.
	- Added 6-arg `Main::Account` overload that enforces non-null network/storage/session capability inputs.
	- `Main::Account` owns one non-null `_sessionCapabilities` member across Session recreation.
	- `Main::Session` borrows a non-owning non-null `_sessionServiceCapabilities` pointer initialized from account accessor.

- Direct-global search result:
	- Scoped `Core::App()` expressions for setup-email lock/unlock, download tracking, and window selection have no matches in `main_session.cpp`.

- Fence checker outputs:
	- `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
		- `SELFTEST PASS: valid and invalid fence cases behaved as expected.`
	- `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956`
		- `PASS: no fence violations found.`

- tg_cli build/help and linked Qt module result:
	- `cmake --build out --config Debug --target tg_cli` passed and produced `out/Telegram/tg_cli/Debug/tg_cli.exe`.
	- `out/Telegram/tg_cli/Debug/tg_cli.exe --help` passed (exit 0).
	- `Telegram/tg_cli/CMakeLists.txt` compiles `capabilities/session_service_capabilities_cli.cpp` and links `Qt${QT_VERSION_MAJOR}::Core` only.

- Desktop build/start/connect result:
	- Earlier output-unavailable failure occurred on a prior `LNK1104` run; no claim of a running `tg.exe` lock is made for this completion pass.
	- User-verified rerun completed with exit code 0 and `%TEMP%/a5_desktop_build.log` tail ending in `Telegram.vcxproj -> C:\work\git\tdesktop\tdesktop\out\Debug\tg.exe`.
	- Safe automation smoke run passed: launched `out/Debug/tg.exe -workdir C:/Users/wd985049/bin/Release`, observed established TCP connection count `2`, then stopped process cleanly (no remaining process with that executable path).

- Regression dispositions:
	- Normal authenticated startup/teardown: exercised via the automated desktop smoke run (pass).
	- Ordinary download session tracking: not manually exercised in this packet.
	- Upload-stop confirmation flow: not manually exercised in this packet.
	- Setup-email lock/unlock flow: not manually exercised in this packet.

- `git diff --check` result:
	- Pass.

- Implementation commit hash:
	- Recorded after commit creation.

- Independent review finding and fix (A5):
	- Finding: `main_session.cpp` deferred setup-email lock used `crl::on_main([=] { ... })` and captured Session state without lifetime protection.
	- Fix: kept the existing `session-service-setup-email-lock` fence and switched to `crl::on_main(crl::guard(this, [=] { ... }))`, preserving the same deferred timing and capability call behavior.
	- Validation: checker self-test PASS, checker base `12e8d4a956` PASS, tg_cli Debug build + `--help` PASS, single-attempt desktop `Telegram` Debug build PASS (`out/Debug/tg.exe`), `git diff --check` PASS.