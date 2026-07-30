# PLAN_tg_probe_22_a6_domain_lifecycle_account_factory
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] A6 completed: desktop Domain seam + owner account-factory routing validated; CLI bundle/runtime closure remains split to A7 **high**

## Goal
Inject Domain lifecycle capabilities into Main::Domain and route every Storage::Domain account creation path through an owner-provided account factory that selects desktop or CLI account capabilities per account instance.

This packet defines A6 desktop seam implementation only. Do not perform A7 construction/runtime closure probes here.

## Hard Constraints
1. Preserve source compatibility for `Main::Domain::Domain(const QString &dataName)`.
2. Do not add another wide Main::Account constructor overload. Avoid constructor explosion.
3. Capability creation for Main::Account must be per-account and move-only safe; never reuse a moved `std::unique_ptr`.
4. No `lib_*` changes.
5. No real tg_cli profile access in A6 validation.
6. All protected-source edits must be fenced.
7. A6 must not target `MTP::Instance` construction; it remains an A7 blocker/probe concern due to existing `Core::App` dependencies.
8. A6 must not declare or implement `CreateCliDomainCapabilityBundle()`.
9. A6 must not alter tg_cli linkage or tg_cli source closure.

## Source Audit Baseline (must stay exhaustive)

### Direct globals in Main::Domain to convert in A6
Observed in `Telegram/SourceFiles/main/main_domain.cpp`:
- Core::App calls: lines 37, 40, 148, 264, 292, 309, 312, 321, 322, 379, 380, 401, 402, 408, 409, 418, 445, 449, 452, 459.
- crl::on_main calls: lines 39, 72, 366, 487, 499.

No direct Core::App or crl::on_main call exists in `Telegram/SourceFiles/storage/storage_domain.cpp`.

### Main::Account construction sites to route through owner factory
Observed in `Telegram/SourceFiles/storage/storage_domain.cpp`:
1. `start(...)` legacy path: `std::make_unique<Main::Account>(_owner, _dataName, 0)`.
2. `startModern(...)` loop path: `std::make_unique<Main::Account>(_owner, _dataName, index)`.
3. `startFromScratch(...)` path: `std::make_unique<Main::Account>(_owner, _dataName, 0)`.

Observed in `Telegram/SourceFiles/main/main_domain.cpp`:
4. `Domain::add(...)` path: `std::make_unique<Account>(this, _dataName, index)`.

All four paths must allocate a fresh capability set for each account construction.

## Interface Correction: callback types

Current `DomainLifecycleCapabilities` uses `std::function<void()>` for run/schedule APIs. Local conventions and Core::Application signatures are narrower:
- `Core::Application::postponeCall(FnMut<void()> &&)` is move-only callable oriented.
- `Core::Application::preventOrInvoke(Fn<void()> &&)` is copyable callable oriented.

### Required correction in A6
In `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h`:
1. Include `base/basic_types.h`.
2. Replace callback signatures:
- `runOnMain(std::function<void()> callback)` -> `runOnMain(FnMut<void()> &&callback)`.
- `postponeCall(std::function<void()> callback)` -> `postponeCall(FnMut<void()> &&callback)`.
- `preventOrInvoke(std::function<void()> callback)` -> `preventOrInvoke(Fn<void()> &&callback)`.

Rationale:
- `runOnMain` and `postponeCall` are one-shot scheduling operations and should allow move-only captures.
- `preventOrInvoke` must remain compatible with Core::Application API expecting `Fn<void()> &&`.

## Capability Bundle And Account Factory Design

### New capability structures (TG-owned, additive)
Add to `Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h`:

```cpp
struct AccountCapabilityBundle {
    std::unique_ptr<AccountNetworkCapabilities> network;
    std::unique_ptr<StorageSettingsCapabilities> storage;
    std::unique_ptr<SessionServiceCapabilities> session;
};

class DomainAccountFactoryCapabilities {
public:
    virtual ~DomainAccountFactoryCapabilities() = default;
    [[nodiscard]] virtual AccountCapabilityBundle createAccountCapabilityBundle() = 0;
};

struct DomainCapabilityBundle {
    std::unique_ptr<DomainLifecycleCapabilities> lifecycle;
    std::unique_ptr<DomainAccountFactoryCapabilities> accountFactory;
};

[[nodiscard]] DomainCapabilityBundle CreateDesktopDomainCapabilityBundle();
```

Design rules:
1. `createAccountCapabilityBundle()` must return a fresh bundle on every call.
2. Each returned pointer must be non-null.
3. Bundle ownership is transferred exactly once into Main::Account construction.
4. No cached/reused unique_ptr fields in domain/factory implementations.

### A6 selection and composition
1. A6 desktop selection is provided by `CreateDesktopDomainCapabilityBundle()` only.
2. Desktop factory composes:
- `CreateDesktopAccountNetworkCapabilities()`
- `CreateDesktopStorageSettingsCapabilities()`
- `CreateDesktopSessionServiceCapabilities()`
3. A6 does not declare or implement CLI domain bundle/factory wiring.

### Deferred to A7 (explicit split)
1. `CreateCliDomainCapabilityBundle()` declaration and implementation.
2. CLI bundle/factory implementations and synthetic Domain/Account construction closure.
3. Shared CLI fallback production config state initialized with `MTP::Config(MTP::Environment::Production)` and consumed by both lifecycle and per-account network capabilities.
4. CLI account-network inert proxy producer behavior (`rpl::never<ProxyChange>()` + no-op notification/rotation methods).
5. tg_cli executable linkage/source closure decisions tied to mtproto prelude/scheme requirements.

## Main::Domain API and ownership

### Source-compatible constructor requirement
Keep existing:
```cpp
explicit Domain(const QString &dataName);
```

Add overload:
```cpp
Domain(const QString &dataName, TgCli::Capabilities::DomainCapabilityBundle capabilityBundle);
```

Delegation rule:
- Existing constructor delegates to the new overload with `CreateDesktopDomainCapabilityBundle()`.

### New members in Main::Domain
Own exactly:
- `_domainLifecycleCapabilities` (`std::unique_ptr<DomainLifecycleCapabilities>`)
- `_domainAccountFactoryCapabilities` (`std::unique_ptr<DomainAccountFactoryCapabilities>`)

Enforce non-null in constructor preconditions.

### Owner account factory API
Add to Main::Domain public interface for Storage::Domain use:
```cpp
[[nodiscard]] std::unique_ptr<Main::Account> createAccountForStorage(int index);
```

Implementation contract:
1. Request a fresh `AccountCapabilityBundle` from `_domainAccountFactoryCapabilities` for each call.
2. Validate all three pointers are non-null.
3. Construct account with existing six-arg constructor:
```cpp
std::make_unique<Main::Account>(
    this,
    _dataName,
    index,
    std::move(bundle.network),
    std::move(bundle.storage),
    std::move(bundle.session));
```

No new Main::Account constructor overload is allowed in A6.

## Exact protected-source substitutions

### Main::Domain lifecycle substitutions
Replace only the scoped globals already inventoried in A1.

Required method mapping:
- `startSettingsAndBackground` -> `_domainLifecycleCapabilities->startSettingsAndBackground()`.
- `createManager` path -> `_domainLifecycleCapabilities->runOnMain(...)` + `createNotificationsManager()`.
- accounts order -> `_domainLifecycleCapabilities->accountsOrder()`.
- postponed unread update -> `_domainLifecycleCapabilities->postponeCall(...)`.
- fallback config copy -> `_domainLifecycleCapabilities->fallbackProductionConfigCopy()`.
- main-menu shown read/write + delayed save -> `_domainLifecycleCapabilities->{mainMenuAccountsShown,setMainMenuAccountsShown,saveSettingsDelayed}`.
- separate window lookups/ensure -> `_domainLifecycleCapabilities->{separateWindowFor,ensureSeparateWindowFor}`.
- passcode/remove unlock path -> `_domainLifecycleCapabilities->{passcodeLocked,unlockPasscode,setSystemUnlockEnabled,saveSettingsDelayed}`.
- refresh fallback config -> `_domainLifecycleCapabilities->refreshFallbackProductionConfig(...)`.
- maybeActivate deferred activation -> `_domainLifecycleCapabilities->preventOrInvoke(...)`.
- all remaining crl::on_main scheduling sites -> `_domainLifecycleCapabilities->runOnMain(...)`.

### Storage::Domain account construction substitutions
Replace all three constructor sites with owner factory:
- `_owner->createAccountForStorage(0)` in legacy and scratch paths.
- `_owner->createAccountForStorage(index)` in modern loop path.

### Main::Domain add() path substitution
Replace direct local construction in `Domain::add(...)` with `createAccountForStorage(index)` so repeated account creation shares the same capability-selection path and move-only semantics.

## Fence IDs

### Telegram/SourceFiles/main/main_domain.h
- `domain-capability-bundle-forward-declarations`
- `domain-capability-constructor-overload`
- `domain-account-factory-method`
- `domain-capability-members`

### Telegram/SourceFiles/main/main_domain.cpp
- `domain-capability-bundle-include`
- `domain-capability-constructor-delegation`
- `domain-capability-constructor-overload`
- `domain-lifecycle-start-settings-background`
- `domain-lifecycle-notification-manager`
- `domain-lifecycle-on-main-export-suggest`
- `domain-lifecycle-accounts-order`
- `domain-lifecycle-postpone-unread-badge`
- `domain-lifecycle-fallback-config-copy`
- `domain-lifecycle-main-menu-settings`
- `domain-lifecycle-window-selection`
- `domain-lifecycle-on-main-remove-redundant`
- `domain-lifecycle-close-account-windows`
- `domain-lifecycle-passcode-transitions`
- `domain-lifecycle-refresh-fallback-config`
- `domain-lifecycle-prevent-or-invoke`
- `domain-lifecycle-activate-window`
- `domain-lifecycle-on-main-remove-redundant-after-activate`
- `domain-lifecycle-on-main-write-accounts`
- `domain-account-factory-implementation`
- `domain-add-account-factory-usage`

### Telegram/SourceFiles/storage/storage_domain.cpp
- `storage-domain-owner-account-factory`

### Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h
- `domain-capability-bundle-types`
- `domain-capability-callback-signatures`
- `domain-capability-bundle-factories`

### Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.cpp
- TG-owned file section updates only (no fences required).

## Ordered Edits
1. Update `domain_lifecycle_capabilities.h` with callback signature correction and bundle/factory type declarations.
2. Update `domain_lifecycle_capabilities.cpp` desktop implementation for corrected callback signatures (if supported by desktop call sites) and add desktop bundle factory + desktop account-factory implementation.
3. Add Main::Domain constructor overload, capability ownership members, and owner account factory declaration/definition while preserving `Domain(const QString&)` delegation.
4. Replace all inventoried Main::Domain lifecycle globals with capability calls.
5. Replace all Storage::Domain Main::Account construction sites with `_owner->createAccountForStorage(...)`.
6. Replace Main::Domain::add direct account construction with `createAccountForStorage(index)`.
7. Run A6 validation sequence (including tg_cli unchanged build/help and static freshness checks) before touching parent plans.
8. Update completion records in packet 14 and this packet only after all checks pass.

## Validation (A6 only)
1. `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
2. `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956`
3. `cmake --build out --config Debug --target tg_cli`
4. `out/Telegram/tg_cli/Debug/tg_cli.exe --help`
5. `cmake --build out --config Debug --target Telegram`
6. Desktop smoke run with existing safe workdir only (no tg_cli profile open).
7. Static factory freshness checks:
- Verify each of the four account construction paths routes through owner factory (`createAccountForStorage(...)`).
- Verify each factory call obtains a new non-null account capability bundle and moves ownership exactly once into Main::Account construction.
8. `git diff --check`
9. Verify no direct `Core::App()` or `crl::on_main(` remains in `main_domain.cpp` outside fenced replacement blocks.
10. Verify no direct `std::make_unique<Main::Account>` remains in `storage_domain.cpp` and `main_domain.cpp` add-path.

Validation restrictions:
- Do not open a real profile from tg_cli.
- Do not run A7 synthetic construction in A6.

## Pass Criteria
1. `Main::Domain::Domain(const QString&)` remains source-compatible.
2. Main::Domain and Storage::Domain account construction uses owner factory path consistently.
3. Every account creation obtains fresh move-only capability objects.
4. Main::Domain lifecycle behavior remains desktop-equivalent.
5. Desktop domain capability bundle/factory compiles and is the only bundle/factory provided in A6.
6. Fence checker and `git diff --check` pass.

## Stop Conditions
1. Any proposal requires Main::Account constructor expansion beyond current six-arg overload.
2. Any capability design caches/reuses moved unique_ptr capability instances.
3. Any required edit touches `lib_*`.
4. Any required behavior introduces real tg_cli profile access in A6.
5. Main::Domain logic requires hard dependency on Window::Controller for non-optional flow.
6. Callback signature correction cannot compile without widening back to std::function.
7. Any required A6 behavior attempts to resolve CLI mtproto linkage/runtime closure instead of deferring to A7.

## Bounded Probe Record (2026-07-30)
Scope limit: linkage/source wiring only, no protected-source edits, max 3 closure expansions.

Attempt 0 (baseline broad linkage already present)
- CMake wiring: `tg_cli` linked `tdesktop::td_mtproto` and `tdesktop::td_scheme`.
- Build: `cmake --build out --config Debug --target tg_cli` failed at link with 11 unresolved externals.
- Exact unresolved symbols:
    - `MTP::details::AbstractConnection::parseNotSecureResponse(...) const`
    - `MTP::details::AbstractConnection::receivedData()`
    - `MTP::details::AbstractConnection::extendedNotSecurePadding() const`
    - `MTP::details::AbstractConnection::staticMetaObject`
    - `Logs::DebugEnabled()`
    - `Logs::started()`
    - `Logs::writeMtp(int, QString const &)`
    - `MTP::details::GetNextRequestId()`
    - `MTP::Instance::cancel(int)`
    - `MTP::Instance::sendRequest(...)`
    - `tl::utf16(QByteArray const &)`

Attempt 1 (narrow source wiring)
- Removed `tdesktop::td_mtproto` / `tdesktop::td_scheme` from `tg_cli`.
- Added source: `../SourceFiles/mtproto/mtproto_config.cpp`.
- Build result: compile failure, missing include `base/bytes.h` from mtproto header chain.

Attempt 2 (source/link expansion #2)
- Added source: `../SourceFiles/mtproto/mtproto_dc_options.cpp`.
- Added low-level targets: `desktop-app::lib_base`, `desktop-app::lib_tl`.
- Build result: compile failure due missing mtproto prelude/type context (`DcId`, `MTPDcOption`, `base::flat_map`, `base::flat_set`, `rpl::event_stream`, and related declarations).

Attempt 3 (source/link expansion #3)
- Forced include for those two source files: `/FI../SourceFiles/mtproto/mtproto_pch.h`.
- Build result: compile failure, missing generated header `scheme.h` from mtproto PCH include chain.

Final probe disposition
- Broad linkage was disproved and removed from `tg_cli` CMake.
- Temporary attempt source/link wiring was removed; worktree now keeps only QtCore direct linkage for `tg_cli`.
- Probe remained below forbidden edits (`lib_*`, protected source implementation) and stopped at bounded expansion limit.

## Scope Split Rationale (2026-07-31)
1. Bounded linkage probes in this packet recorded three non-converging expansions from `tdesktop::td_mtproto` dependency attempts to mtproto prelude/generated scheme closure requirements.
2. Those failures are preserved here as evidence and are not re-opened in A6.
3. A6 is narrowed to a desktop seam extraction that remains source-compatible (`Domain(const QString&)`) and routes all four account-construction paths through owner factory creation.
4. CLI bundle/factory implementation and executable closure are explicitly deferred to A7, where synthetic construction/runtime probing can own the closure decision.

## Explicitly Deferred
1. A7 declaration/implementation of `CreateCliDomainCapabilityBundle()`.
2. A7 CLI bundle/factory implementations, including shared fallback `MTP::Config(MTP::Environment::Production)` state across lifecycle + per-account network capabilities.
3. A7 CLI never-proxy producer behavior (`proxyChanges() -> rpl::never<ProxyChange>()`) and related no-op proxy notifications/rotation hooks.
4. A7 executable tg_cli linkage/source closure and synthetic Domain/Account/Session runtime probing.
5. A8 profile ownership/workdir resolution.
6. Any tg_cli command path beyond `--help`.
7. Any attempt to resolve `MTP::Instance`/`Core::App` construction coupling in A6.

## Completion Record Template
Before marking A6 [DONE], append:
- exact files changed;
- exact fence IDs touched;
- callback signature correction outcome and final method signatures;
- account factory usage confirmation for all four construction paths;
- fence checker outputs;
- tg_cli build/help output;
- desktop build/smoke outcome;
- `git diff --check` output;
- implementation commit hash.

## Completion Record (A6, 2026-07-31)
- Exact files changed:
    - Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h
    - Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.cpp
    - Telegram/SourceFiles/main/main_domain.h
    - Telegram/SourceFiles/main/main_domain.cpp
    - Telegram/SourceFiles/storage/storage_domain.cpp
    - TG_PROBES/PLAN_tg_probe_22_a6_domain_lifecycle_account_factory.md
    - TG_PROBES/PLAN_tg_probe_14_core_extraction.md
    - PLAN_tg_console_mode.md
- Exact fence IDs touched:
    - Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h:
        - domain-capability-callback-signatures
        - domain-capability-bundle-types
        - domain-capability-bundle-factories
    - Telegram/SourceFiles/main/main_domain.h:
        - domain-capability-bundle-forward-declarations
        - domain-capability-constructor-overload
        - domain-account-factory-method
        - domain-capability-members
    - Telegram/SourceFiles/main/main_domain.cpp:
        - domain-capability-bundle-include
        - domain-capability-constructor-delegation
        - domain-capability-constructor-overload
        - domain-lifecycle-start-settings-background
        - domain-lifecycle-notification-manager
        - domain-lifecycle-on-main-export-suggest
        - domain-lifecycle-accounts-order
        - domain-lifecycle-postpone-unread-badge
        - domain-lifecycle-fallback-config-copy
        - domain-lifecycle-main-menu-settings
        - domain-lifecycle-window-selection
        - domain-lifecycle-on-main-remove-redundant
        - domain-lifecycle-close-account-windows
        - domain-lifecycle-passcode-transitions
        - domain-lifecycle-refresh-fallback-config
        - domain-lifecycle-prevent-or-invoke
        - domain-lifecycle-activate-window
        - domain-lifecycle-on-main-remove-redundant-after-activate
        - domain-lifecycle-on-main-write-accounts
        - domain-account-factory-implementation
        - domain-add-account-factory-usage
    - Telegram/SourceFiles/storage/storage_domain.cpp:
        - storage-domain-owner-account-factory
- Callback signature correction outcome and final method signatures:
    - Applied in `DomainLifecycleCapabilities`:
        - `runOnMain(FnMut<void()> &&callback)`
        - `postponeCall(FnMut<void()> &&callback)`
        - `preventOrInvoke(Fn<void()> &&callback)`
    - Desktop implementation now forwards these types directly to `crl::on_main`, `Core::App().postponeCall`, and `Core::App().preventOrInvoke`.
- Account factory usage confirmation for all four construction paths:
    - Storage legacy path: `_owner->createAccountForStorage(0)`.
    - Storage modern loop path: `_owner->createAccountForStorage(index)`.
    - Storage scratch path: `_owner->createAccountForStorage(0)`.
    - Main::Domain add path: `createAccountForStorage(index)`.
- Static freshness confirmation:
    - `createAccountForStorage(...)` call sites are exactly 4 (3 in `storage_domain.cpp`, 1 in `main_domain.cpp` add path).
    - `createAccountForStorage(...)` obtains a fresh bundle per call via `_domainAccountFactoryCapabilities->createAccountCapabilityBundle()`, asserts all three pointers non-null, and transfers ownership exactly once with `std::move(bundle.network/storage/session)` into the six-arg `Main::Account` constructor.
- Fence checker outputs:
    - `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test` => `SELFTEST PASS: valid and invalid fence cases behaved as expected.`
    - `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956` => `PASS: no fence violations found.`
- tg_cli build/help output:
    - `cmake --build out --config Debug --target tg_cli` => success (`tg_cli.vcxproj -> .../out/Telegram/tg_cli/Debug/tg_cli.exe`).
    - `out/Telegram/tg_cli/Debug/tg_cli.exe --help` => usage/help printed, exit success.
- Desktop build/smoke outcome:
    - `cmake --build out --config Debug --target Telegram` => success (`Telegram.vcxproj -> .../out/Debug/tg.exe`), with non-fatal linker warnings `LNK4099` for `windows_toastactivator_i.obj`/`windows_quiethours_i.obj` PDB lookup.
    - Desktop smoke: `out/Debug/tg.exe -workdir C:/Users/wd985049/bin/Release` executed in this packet with no error output and no tg_cli profile open.
- `git diff --check` output:
    - Clean (no output).
- Implementation commit hash:
    - Pending commit in this packet (filled immediately after commit).
- Skipped coverage (A6 restrictions):
    - No `CreateCliDomainCapabilityBundle()` declaration/implementation.
    - No tg_cli CMake/source linkage edits.
    - No A7 synthetic construction/runtime closure probes.
    - No real tg_cli profile access.