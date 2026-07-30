# PLAN_tg_probe_12_executable_qt_runtime
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_executable_probe_results.md
Status: [DONE] BLOCKED - Probe 11 produced no bounded session executable; skeleton QCoreApplication success does not prove session runtime requirements.

## Question
For the bounded session probe closure, does Windows x64 require QCoreApplication, QGuiApplication, or QApplication?

## Probe Design
1. Run the bounded probe first with QCoreApplication.
2. Capture compile/link/runtime evidence for GUI-specific requirements.
3. If required, move only the probe entrypoint to QGuiApplication, then QApplication.
4. Never create/show a window.
5. Record linked Qt modules and PE dependencies for each viable level.

## Pass Criteria
- One application class initializes the bounded probe deterministically without windows.

## Fail Criteria
- Initialization requires window/controller construction or interactive GUI behavior.

## Validation
- Run probe with --runtime-probe and deterministic timeout/exit.
- dumpbin /dependents on the resulting executable.
