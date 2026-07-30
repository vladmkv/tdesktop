# PLAN_tg_probe_05_account_selection
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL PASS; early account enumeration path identified.

## Question
At what point can tg_cli enumerate desktop accounts and select one without fully initializing UI-bound sessions?

## Scope
- Storage::Domain/Main::Domain account metadata, active account behavior, account startup lifecycle.

## Procedure
1. Trace account list persistence and load sequence.
2. Identify metadata available before Main::Session/Data::Session construction.
3. Determine how selected/active account is represented.
4. Define stable CLI selection display and process-lifetime ownership.
5. Identify side effects of starting only one account.

## Pass Criteria
- Accounts can be enumerated and one selected before UI-bound session startup, or with a bounded reusable startup path.

## Fail Criteria
- Account identity is unavailable until full UI-coupled session initialization.

## Output
Document sequence, symbols, metadata fields safe to display, and verdict.
