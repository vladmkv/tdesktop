# PLAN_tg_probe_17_a3_account_network_injection
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Completed and validated on tg-cli **high**

## Goal
Inject `AccountNetworkCapabilities` into `Main::Account` without changing existing desktop call sites or behavior.

This task does not open a profile, construct a CLI account, or start MTProto. It establishes one capability seam and proves desktop compatibility.

## Prerequisite Corrections (must complete first)

### A0.1 Fence checker and branch compliance
1. Fix `check_tg_change_fences.py` so mixed hunks validate deletion overlap as well as every added line.
2. Add self-tests for:
- mixed hunk with fenced additions and unfenced deletion (must fail)
- valid fenced replacement (must pass)
- adjacent blocks
- duplicate IDs
3. Retrofit named fences around TG changes in existing files:
- `AGENTS.md`: `tg-cli-agent-guidance` using Markdown markers.
- `Telegram/build/prepare/prepare.py`: `dav1d-github-mirror` using Python markers.
4. Update `TG_CHANGE_POLICY.md`: all deletion hunks (including mixed hunks) require fenced replacement overlap; remove the obsolete "accepted historical" dav1d exception.
5. Run checker against base `12e8d4a956`; result must be PASS.

### A2.1 Capability baseline corrections
1. In `domain_lifecycle_capabilities.h`, include the direct declaration header for `crl::time` instead of relying on `base/timer.h` transitively.
2. Keep `ProxyChange` as the TG-owned DTO intentionally; document that it isolates the interface from `Core::Application::ProxyChange`.
3. Do not use the desktop capability factory from tg_cli runtime/probes because it calls `Core::App()`.
4. Record desktop A2 validation as pending, not passed.
5. Before a desktop build, require:
- no active `cl`, `mspdbsrv`, MSBuild, Visual Studio debugger, or tg process holding build outputs
- sufficient free disk (target at least 50 GB because prior logs included both PDB failures and `No space left on device`)

## A3 Design

### Existing API preservation
Keep this constructor unchanged:
```cpp
Account(not_null<Domain*> domain, const QString &dataName, int index);
```

Add an overload:
```cpp
Account(
    not_null<Domain*> domain,
    const QString &dataName,
    int index,
    std::unique_ptr<TgCli::Capabilities::AccountNetworkCapabilities> capabilities);
```

The existing constructor delegates to the overload using `CreateDesktopAccountNetworkCapabilities()`.

### Ownership
- `Main::Account` exclusively owns one non-null capability object via `std::unique_ptr`.
- Declare `_networkCapabilities` after `_domain` and before `_local` so future storage capability transit is possible without lifetime inversion.
- Add a non-null reference accessor only if a later stage requires it; A3 Account methods should use the member directly.
- Constructor enforces non-null capability with an existing assertion/contract style.

### Protected files and fence IDs

`Telegram/SourceFiles/main/main_account.h`:
- `account-network-capability-forward-declaration`
- `account-network-capability-overload`
- `account-network-capability-member`

`Telegram/SourceFiles/main/main_account.cpp`:
- `account-network-capability-include`
- `account-network-capability-constructor`
- `account-network-fallback-config`
- `account-network-proxy-changes`
- `account-network-proxy-state-change`

Keep adjacent proxy notify/rotation replacements in one cohesive `account-network-proxy-state-change` block.

### Exact replacements
1. Null config fallback in `Account::start()`:
- replace direct `Core::App().fallbackProductionConfig()` copy with `fallbackProductionConfigCopy()`.
- verify returned pointer is non-null before `startMtp`.

2. `Account::watchProxyChanges()`:
- consume `TgCli::Capabilities::ProxyChange` producer.
- preserve restart/re-init key comparison semantics exactly.

3. MTP state-change handler:
- replace proxy connection notification and rotation calls with capability methods.
- preserve main-DC condition exactly.

### No-go changes
- Do not change `Main::Domain` or `Storage::Domain` in A3.
- Do not attempt CLI Account construction in A3; `Storage::Domain` still calls the desktop constructor.
- Do not change `Main::Session`, `Storage::Account`, lib_* files, proxy semantics, or MTP lifecycle.
- Do not instantiate `CreateDesktopAccountNetworkCapabilities()` from the QCore tg_cli skeleton.

## Validation Order
1. Run fence checker self-test.
2. Run fence checker against `12e8d4a956`.
3. Build `tg_cli` Debug and run `tg_cli.exe --help`.
4. Build desktop `Telegram` Debug only after process/disk preconditions pass.
5. Smoke-run desktop tg and verify startup/connectivity.
6. Manual proxy smoke test:
- open proxy settings
- toggle proxy mode or add/remove a test-disabled proxy without exposing credentials
- verify connection type notification and no crash
7. Re-run fence checker.
8. Inspect diff: only planned fence IDs in protected files.

## Pass Criteria
- Existing desktop constructor and all current call sites remain unchanged.
- Desktop starts and connects with the same behavior.
- Proxy change subscription and MTP state-change behavior remain equivalent.
- tg_cli skeleton still builds/runs.
- Fence checker passes against branch base.
- No protected files outside `main_account.h/.cpp` are changed for A3.

## Stop Conditions
- Existing Account call sites need edits.
- Capability can be null after construction.
- Proxy behavior cannot be preserved exactly.
- Desktop validation fails for a non-environmental reason.
- Fence checker requires bypass/allowlist for an A3 source change.

## Required Plan Update
On completion, update `PLAN_tg_probe_14_core_extraction.md` with:
- status/result
- exact fence IDs
- commit hash
- checker/build/run/proxy smoke outcomes
- any newly discovered capability method (requires explicit review before proceeding to A4)

## Future Ownership Dependency (not part of A3)
CLI capability selection will later require a fenced Account factory path because `Storage::Domain::startModern()` directly constructs `Main::Account` with the legacy constructor. Plan A6 must add a `Main::Domain` account-creation seam and change `Storage::Domain` to call it; otherwise CLI profiles always receive desktop capabilities.

## Results
- Added `Main::Account` overload accepting `std::unique_ptr<TgCli::Capabilities::AccountNetworkCapabilities>` while preserving existing constructor and call sites.
- Existing constructor now delegates to `CreateDesktopAccountNetworkCapabilities()`.
- Added non-null `_networkCapabilities` ownership member between `_domain` and `_local`.
- Replaced direct global usages in `Main::Account` with capability calls for:
  - fallback config copy in `Account::start()`
  - proxy change producer in `Account::watchProxyChanges()`
  - proxy connection notify and rotation in MTP state-change handler
- Kept proxy restart and re-init comparison semantics unchanged.

## Fence IDs Touched
- `account-network-capability-forward-declaration`
- `account-network-capability-overload`
- `account-network-capability-member`
- `account-network-capability-include`
- `account-network-capability-constructor`
- `account-network-fallback-config`
- `account-network-proxy-changes`
- `account-network-proxy-state-change`

## Validation Evidence
1. `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
    - Result: `SELFTEST PASS: valid and invalid fence cases behaved as expected.`
2. `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956`
    - Result: `PASS: no fence violations found.`
3. `cmake --build C:/work/git/tdesktop/tdesktop/out --config Debug --target tg_cli`
    - Result: success.
4. `out/Telegram/tg_cli/Debug/tg_cli.exe --help`
    - Result: success.
5. `cmake --build C:/work/git/tdesktop/tdesktop/out --config Debug --target Telegram -- /m:1 /nodeReuse:false`
    - Result: initial attempts hit environment `C1033` PDB lock; after lock clear and user manual build rerun, `Telegram.vcxproj -> out/Debug/tg.exe` succeeded.
6. Desktop startup/connect smoke:
    - Started `out/Debug/tg.exe -workdir C:/Users/wd985049/bin/Release` and observed established TCP connection count `1`, then closed process.
7. Manual proxy smoke test (user-reported)
    - Completed in desktop settings by adding/toggling a test proxy and restoring state; no crash reported.
8. `git diff --check`
    - Result: success (no output).

## Commit
- Packet commit hash: recorded after commit creation.
