# PLAN_tg_probe_02_qt_application
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL; Windows runtime class remains an executable probe.

## Executable Stage 1 Skeleton Probe
1. [DONE] Reconfigured the existing Windows x64 out tree with `BUILD_TG_CLI=ON`; cached API credentials remained populated.
2. [DONE] Built only the Debug `tg_cli` target; the generated project links Qt5Core/Qt5Cored and no Qt GUI libraries.
3. [DONE] Ran `tg_cli.exe` and `tg_cli.exe --help`; both printed the expected short output and exited 0.
4. [DONE] Verified the executable PE subsystem is Windows CUI and its Qt dependency is Core only.
5. [DONE] Verified protected existing source/library paths remain unchanged.

## Question
Can reused desktop account/session sources run with QCoreApplication, or do they require QApplication and GUI platform initialization?

## Scope
- Read-only call/include analysis; no prototype code yet.
- Startup path, event loop, QObject ownership, timers, networking, clipboard/screen/widget access.

## Procedure
1. Trace startup from current main/launcher/sandbox to account/session creation.
2. Find QApplication/QGuiApplication-specific calls reachable before CLI command execution.
3. Distinguish compile/link dependencies from runtime-required behavior.
4. Identify environment options for headless QApplication if unavoidable.
5. Define the smallest executable runtime probe for later implementation.

## Pass Criteria
- QCoreApplication is sufficient, or QApplication is sufficient without rendering/window creation and with deterministic headless startup.

## Fail Criteria
- Required startup creates interactive GUI/windows or needs display-specific behavior that cannot be isolated in tg_cli-owned code.

## Output
Record minimum Qt application class/modules, required initialization sequence, and PASS/CONDITIONAL/FAIL.
