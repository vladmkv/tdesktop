# PLAN_tg_probe_19_a2_1_baseline_corrections
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Completed and validated on tg-cli **high**

## Goal
Correct small capability-baseline issues and finish the missing desktop A2 validation before Main::Account injection.

## Allowed Files
- Telegram/tg_cli/capabilities/domain_lifecycle_capabilities.h
- Telegram/tg_cli/capabilities/account_network_capabilities.h (documentation-only if needed)
- Telegram/CMakeLists.txt only if switching the existing capability source list to project convention is proven safe
- plan/devlog files

No Main/Storage protected-source injection.

## Tasks
1. Replace transitive `base/timer.h` dependency in domain_lifecycle_capabilities.h with direct `#include <crl/crl_time.h>` for `crl::time`.
2. Document in code or plan (not an empty narration comment) that `TgCli::Capabilities::ProxyChange` is intentionally a TG-owned boundary DTO. Do not replace it with Core::Application::ProxyChange.
3. Confirm tg_cli skeleton does not instantiate any `CreateDesktop*Capabilities` factory; desktop factories require `Core::App()`.
4. Decide whether the existing fenced raw `target_sources` block should use `nice_target_sources`. Change only if the helper supports this source root without platform/filter side effects; otherwise record raw target_sources as intentional.
5. Correct parent plan wording: A2 desktop behavior is unverified, not passed or definitely PDB-locked.

## Environment Preconditions
- Record free disk. Require at least 50 GB before full desktop Debug build, or obtain explicit approval to proceed with less.
- Confirm no active `cl`, `mspdbsrv`, MSBuild, Visual Studio debugger, or build-output tg process.
- A running installed tg is not a PDB lock by itself, but close it for the startup smoke test and to avoid profile conflicts.

## Validation
1. Fence checker self-test and full branch PASS.
2. Build/run tg_cli Debug `--help`.
3. Build desktop Telegram Debug once; no retry on C1033/C1041/LNK1104/access-denied errors.
4. Smoke-run freshly built desktop tg long enough to confirm startup and network connection, then close it.
5. `git diff --check` and diagnostics.

## Pass Criteria
- Direct include dependency is correct.
- tg_cli remains QtCore-only and runs.
- Desktop Debug build succeeds and freshly built tg starts/connects.
- Fence checker passes.

## Failure Classification
- PDB/file lock: stop and ask user to close build/debug processes.
- Disk exhaustion: stop and report required space; do not delete outputs without approval.
- Source/compile/link regression: A2 fails; fix before A3.

## Commit
Separate commit from A0.1 and A3. Suggested subject: `Validate TG capability baseline`.

## Results
- Replaced transitive include with direct `#include <crl/crl_time.h>` in `domain_lifecycle_capabilities.h` for `crl::time`.
- Documented `TgCli::Capabilities::ProxyChange` as an intentional TG-owned boundary DTO in `account_network_capabilities.h`.
- Confirmed tg_cli skeleton does not instantiate any `CreateDesktop*Capabilities` factory.
- Kept the existing fenced raw `target_sources(Telegram PRIVATE ...)` block in `Telegram/CMakeLists.txt` intentionally:
  - `nice_target_sources` in this file is rooted to `SourceFiles` paths (`${src_loc}`), while capability sources live under `tg_cli/`.
  - No safe helper migration was proven without source-root/behavior risk, so no CMake change was made.
- Updated parent plan wording and statuses to remove stale "unverified"/lock-only framing now that A2 desktop validation is complete.

## Fence IDs Touched
- None in this packet (header-only and plan-only corrections).

## Validation Evidence
1. Disk/process preconditions:
	- Free disk observed below policy target; explicit approval granted to proceed under 50 GB.
	- Active build lock-holder processes were cleared before validation attempt.
2. `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
	- Result: `SELFTEST PASS: valid and invalid fence cases behaved as expected.`
3. `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956`
	- Result: `PASS: no fence violations found.`
4. `cmake --build C:/work/git/tdesktop/tdesktop/out --config Debug --target tg_cli`
	- Result: success.
5. `out/Telegram/tg_cli/Debug/tg_cli.exe --help`
	- Result: success.
6. `cmake --build C:/work/git/tdesktop/tdesktop/out --config Debug --target Telegram`
	- Result: success on single attempt; no lock/disk failure classification.
7. Desktop startup/connect smoke:
	- Started `out/Debug/tg.exe`, observed process alive and at least one established TCP connection, then closed process.
8. `git diff --check`
	- Result: success (no output).
