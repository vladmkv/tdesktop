# PLAN_tg_probe_20_a4_storage_settings_injection
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Completed and validated on tg-cli **high**

## Goal
Inject `StorageSettingsCapabilities` into `Storage::Account` through `Main::Account`, preserving all existing constructors, call sites, and desktop behavior.

Replace the six scoped theme and TonSite settings globals in `storage_account.cpp`. Add a tg_cli implementation that has no presentation or TonSite storage side effects. Do not open a profile or construct a CLI account in this packet.

## Prerequisites
1. A0.1, A2.1, and A3 remain complete.
2. Fence checker self-test and full validation against `12e8d4a956` pass before editing.
3. The worktree may contain the known parent-plan update; do not discard or overwrite unrelated user changes.

## Ownership And API Design

### Main::Account overloads
Preserve both existing constructors unchanged:

```cpp
Account(not_null<Domain*> domain, const QString &dataName, int index);

Account(
	not_null<Domain*> domain,
	const QString &dataName,
	int index,
	std::unique_ptr<TgCli::Capabilities::AccountNetworkCapabilities> capabilities);
```

Add a third overload:

```cpp
Account(
	not_null<Domain*> domain,
	const QString &dataName,
	int index,
	std::unique_ptr<TgCli::Capabilities::AccountNetworkCapabilities> networkCapabilities,
	std::unique_ptr<TgCli::Capabilities::StorageSettingsCapabilities> storageCapabilities);
```

Delegation rules:
- The three-argument desktop constructor selects both desktop factories.
- The existing four-argument A3 constructor preserves its API and delegates with the supplied network capability plus desktop storage capabilities.
- The new five-argument constructor owns the network capability and moves the storage capability into `Storage::Account`.
- Enforce both incoming capability pointers as non-null before use or transfer.
- Do not add a storage-capability member to `Main::Account`; ownership transfers to `Storage::Account`.

### Storage::Account overloads
Preserve the existing constructor:

```cpp
Account(not_null<Main::Account*> owner, const QString &dataName);
```

Add:

```cpp
Account(
	not_null<Main::Account*> owner,
	const QString &dataName,
	std::unique_ptr<TgCli::Capabilities::StorageSettingsCapabilities> capabilities);
```

The existing constructor delegates using `CreateDesktopStorageSettingsCapabilities()`. The new constructor owns one non-null capability via `std::unique_ptr`. Declare the member after `_owner` and before fields that may use it during initialization or teardown.

### TonSite free-function compatibility
`Storage::TonSiteStorageId()` has a desktop caller in `iv_instance.cpp` and is not an `Account` member. Preserve the no-argument API and add:

```cpp
[[nodiscard]] Webview::StorageId TonSiteStorageId(
	TgCli::Capabilities::StorageSettingsCapabilities &capabilities);
```

The no-argument function creates desktop storage capabilities and delegates to the overload. The overload performs token read/generation/write/save exclusively through the supplied capability. Do not edit `iv_instance.cpp`; tg_cli does not invoke this unsupported webview path.

## tg_cli Capability Implementation
Add a TG-owned CLI implementation in a separate source file so the tg_cli target does not compile the desktop factory implementation or include `Core::App()` dependencies.

Expose:

```cpp
[[nodiscard]] std::unique_ptr<StorageSettingsCapabilities> CreateCliStorageSettingsCapabilities();
```

CLI behavior:
- `isNightMode()` returns `false` deterministically.
- `setBackgroundTileValues(...)` is a no-op.
- `tonsiteStorageToken()` returns an empty byte array.
- `setTonsiteStorageToken(...)` is a no-op.
- `saveSettingsDelayed()` is a no-op.

Compile the CLI implementation into `tg_cli`. Keep tg_cli QtCore-only in A4. Do not instantiate desktop factories from tg_cli.

## Exact Global Replacements
In `storage_account.cpp`:
1. Legacy background-key selection uses `_settingsCapabilities->isNightMode()`.
2. Read-context tile restoration uses `_settingsCapabilities->setBackgroundTileValues(context.tileDay, context.tileNight)`.
3. The capability-taking `TonSiteStorageId(...)` uses `tonsiteStorageToken()`.
4. Empty-token generation calls `setTonsiteStorageToken(result.token)` and `saveSettingsDelayed()` through the capability.

After the replacement, the scoped `Window::Theme` and `Core::App()` calls must exist only in the desktop capability implementation, not in `storage_account.cpp`.

## Protected Files And Fence IDs

`Telegram/SourceFiles/main/main_account.h`:
- `account-storage-capability-forward-declaration`
- `account-storage-capability-overload`

`Telegram/SourceFiles/main/main_account.cpp`:
- Extend the existing `account-network-capability-include` block to include the storage capability declaration, or use `account-storage-capability-include` if a separate include is cleaner.
- `account-storage-capability-constructor`

Do not rewrite the A3 constructor block wholesale. Keep the A3 network ownership and behavior intact while adding the smallest adjacent delegation block.

`Telegram/SourceFiles/storage/storage_account.h`:
- `storage-settings-capability-forward-declaration`
- `storage-settings-capability-overload`
- `storage-settings-capability-member`
- `storage-settings-tonsite-overload`

`Telegram/SourceFiles/storage/storage_account.cpp`:
- `storage-settings-capability-include`
- `storage-settings-capability-constructor`
- `storage-settings-night-mode`
- `storage-settings-background-tiles`
- `storage-settings-tonsite-storage-id`

TG-owned files under `Telegram/tg_cli/` require no fences. Do not modify `lib_*`, generated, vendored, or prepared-output files.

## Implementation Order
1. Run the pre-edit fence checks.
2. Add the CLI factory declaration/implementation and compile it into tg_cli.
3. Add the `Storage::Account` overload, ownership member, and constructor delegation.
4. Replace night-mode and background-tile globals.
5. Add the capability-taking TonSite overload and preserve the no-argument desktop API.
6. Add the five-argument `Main::Account` overload and constructor delegation.
7. Run the focused validations before any further plan or source edits.
8. Record results in this packet and the parent plan only after all required checks pass.

## Validation Order
1. Fence checker self-test.
2. Fence checker against base `12e8d4a956`.
3. Search `storage_account.cpp` and confirm no direct `Window::Theme::` or `Core::App()` calls remain.
4. Build tg_cli Debug and run `tg_cli.exe --help`; confirm the target still links QtCore only.
5. Build desktop Telegram Debug once after process/disk preconditions pass.
6. Smoke-run the freshly built desktop tg against the established test workdir and confirm startup/connectivity.
7. Desktop regression checks:
   - open the established profile successfully;
   - switch day/night appearance and restart once to verify background/theme restoration;
   - exercise the existing TonSite path only if already configured and safe; otherwise verify token round-trip with a narrow non-profile test or record the path as not manually exercised.
8. Re-run fence checker and `git diff --check`.
9. Inspect the final diff: only planned protected files and TG-owned capability/CMake/plan files changed.

Do not run tg and tg_cli against the same real profile in A4. A4 has no shared-profile CLI test.

## Pass Criteria
- All pre-A4 constructors remain source-compatible.
- Desktop constructors select desktop network and storage capabilities.
- `Storage::Account` exclusively owns a non-null storage capability.
- The CLI storage implementation compiles without `Core::App()`, QtGui, QtWidgets, or `lib_ui` dependencies.
- Scoped globals are absent from `storage_account.cpp`.
- Desktop profile startup and theme behavior remain equivalent.
- tg_cli remains QtCore-only and `--help` exits successfully.
- Fence checker and `git diff --check` pass.

## Stop Conditions
- Any existing constructor or desktop call site must be removed or changed incompatibly.
- `StorageSettingsCapabilities` must become a broad Core/Application facade.
- Replacing the TonSite helper requires changing its existing `iv_instance.cpp` caller.
- tg_cli must link the desktop capability implementation or `Core::App()`.
- Any `lib_*` modification is required.
- Desktop theme/profile behavior regresses.
- Fence validation requires an exception or allowlist.
- A real shared profile must be opened by tg_cli to validate A4.

## Explicitly Deferred
- Session service injection: A5.
- Domain lifecycle and account-factory selection: A6.
- CLI Domain/Account/Session construction: A7.
- Shared-profile ownership, passcode, account enumeration, and all commands: A8-A10.

## Required Completion Record
Before marking this packet `[DONE]`, append:
- exact files and fence IDs changed;
- constructor delegation and ownership outcome;
- direct-global search result;
- fence checker outputs;
- tg_cli build/help result and linked Qt module check;
- desktop build/start/connect and theme regression results;
- TonSite validation disposition;
- commit hash after the implementation is committed.

## Completion Record
- Exact files changed:
	- `Telegram/SourceFiles/main/main_account.h`
	- `Telegram/SourceFiles/main/main_account.cpp`
	- `Telegram/SourceFiles/storage/storage_account.h`
	- `Telegram/SourceFiles/storage/storage_account.cpp`
	- `Telegram/tg_cli/capabilities/storage_settings_capabilities.h`
	- `Telegram/tg_cli/capabilities/storage_settings_capabilities_cli.cpp`
	- `Telegram/tg_cli/CMakeLists.txt`
	- `TG_PROBES/PLAN_tg_probe_14_core_extraction.md`
	- `TG_PROBES/PLAN_tg_probe_20_a4_storage_settings_injection.md`
- Fence IDs changed in protected files:
	- `account-storage-capability-forward-declaration`
	- `account-storage-capability-overload`
	- `account-storage-capability-constructor`
	- `storage-settings-capability-forward-declaration`
	- `storage-settings-capability-overload`
	- `storage-settings-capability-member`
	- `storage-settings-tonsite-overload`
	- `storage-settings-capability-include`
	- `storage-settings-capability-constructor`
	- `storage-settings-night-mode`
	- `storage-settings-background-tiles`
	- `storage-settings-tonsite-storage-id`

- Constructor delegation and ownership outcome:
	- `Main::Account` keeps existing 3-arg and 4-arg constructors source-compatible.
	- Added 5-arg constructor taking network + storage capabilities.
	- 3-arg constructor delegates to desktop network + desktop storage factories.
	- Existing 4-arg constructor delegates to provided network + desktop storage factory.
	- `Storage::Account` keeps existing constructor and delegates to desktop storage factory.
	- Added `Storage::Account` overload with explicit storage capability ownership.
	- `Storage::Account` owns one non-null `_settingsCapabilities` member.
	- `Main::Account` does not store a storage-capability member; ownership transfers to `Storage::Account`.

- Direct-global search result:
	- `storage_account.cpp` contains no direct `Window::Theme::` or `Core::App()` references after replacement.

- Fence checker outputs:
	- `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
		- `SELFTEST PASS: valid and invalid fence cases behaved as expected.`
	- `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956`
		- `PASS: no fence violations found.`

- tg_cli build/help and Qt link check:
	- `cmake --build C:/work/git/tdesktop/tdesktop/out --config Debug --target tg_cli` passed.
	- `out/Telegram/tg_cli/Debug/tg_cli.exe --help` passed.
	- `Telegram/tg_cli/CMakeLists.txt` links only `Qt${QT_VERSION_MAJOR}::Core` and compiles `storage_settings_capabilities_cli.cpp` (no desktop factory linkage).

- Desktop build/start/connect and theme regression:
	- `cmake --build C:/work/git/tdesktop/tdesktop/out --config Debug --target Telegram -- /m:1 /nodeReuse:false` passed.
	- Startup/connect smoke with established workdir passed (`EstablishedTcpConnections` observed > 0).
	- Manual regression (user-reported): startup/connect pass, day/night toggle pass, restart restoration pass, no crash.

- TonSite validation disposition:
	- Manual TonSite path: skipped by user (not exercised).

- Commit hash:
	- Recorded after commit creation.