# PLAN_tg_probe_10_dependency_floor
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL; staged dependency reduction order defined.

## Question
What is the lowest realistic Qt/UI dependency floor for the chosen selected-desktop-source architecture?

## Scope
- Static source/CMake analysis now; measured link/runtime graph later.
- QtCore, QtNetwork, QtGui, QtWidgets, lib_ui and UI-bound source clusters.

## Procedure
1. Combine findings from Probes 01, 02, 05-09.
2. Classify dependencies as protocol/runtime-required, high-level-model-required, link-only, or removable.
3. Order removable clusters by low-to-high risk.
4. Define a behavior-preserving stripping sequence and stop conditions.
5. Estimate deployment/runtime implications on Windows x64.

## Pass Criteria
- A staged reduction path exists with explicit tests after each cluster removal.

## Fail Criteria
- Dependencies are too entangled to reduce without protected-source refactoring; document accepted floor instead.

## Output
Provide dependency matrix, stripping order, accepted floor, and PASS/CONDITIONAL/FAIL.
