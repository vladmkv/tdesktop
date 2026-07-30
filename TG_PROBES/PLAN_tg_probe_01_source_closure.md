# PLAN_tg_probe_01_source_closure
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL; executable closure measurement remains in parent Stage 2.

## Question
What exact existing Main/Data/History source closure is required to construct and operate one desktop account/session in tg_cli?

## Scope
- Read-only static build/source analysis.
- Start from Main::Domain, Main::Account, Main::Session, Storage::Domain, Storage::Account.
- Include chat/history objects only far enough to identify the boundary needed by later commands.

## Procedure
1. Trace constructors, ownership, and direct calls from domain -> account -> session -> data/history.
2. Map each implementation file to its current CMake target.
3. Separate reusable lib targets from files compiled directly into Telegram.
4. Identify UI/window/history-view dependencies and their source closure.
5. Classify every required item as reusable target, selected existing source, tg_cli adapter candidate, or blocker.

## Evidence
- Exact file and symbol references.
- Dependency table with reason each source/target is required.
- A minimal proposed target_link_libraries/source list.

## Pass Criteria
- Finite, explainable closure with no protected-source edits.
- Estimated closure is maintainable across upstream merges.

## Fail Criteria
- Closure approaches most of Telegram target or requires cyclic UI startup behavior.
- Required behavior is private/inaccessible without protected-source edits.

## Output
Write findings to ../NOTE_tg_console_mode.md under Probe 01 and recommend PASS, CONDITIONAL, or FAIL.
