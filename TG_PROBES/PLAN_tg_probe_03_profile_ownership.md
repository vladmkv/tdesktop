# PLAN_tg_probe_03_profile_ownership
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL; fail-closed IPC ownership algorithm identified.

## Question
How can tg_cli reliably prevent concurrent access to the desktop profile while remaining compatible with tg?

## Scope
- Desktop workdir resolution, single-instance protocol, QLockFile/QLocalServer behavior, storage atomic writes.
- Windows x64 behavior first.

## Procedure
1. Trace default/custom/portable workdir selection.
2. Trace tg single-instance lock/server names and executable/workdir inputs.
3. Determine whether tg_cli can detect an active tg without changing tg.
4. Audit storage-layer locking and atomicity.
5. Propose fail-closed ownership protocol for tg_cli and identify race windows.

## Pass Criteria
- tg_cli can detect/acquire exclusive profile ownership before opening tdata and fail closed when tg is active.

## Fail Criteria
- No reliable external signal exists and safe ownership would require modifying tg.

## Output
Document algorithm, race analysis, error cases, and PASS/CONDITIONAL/FAIL.
