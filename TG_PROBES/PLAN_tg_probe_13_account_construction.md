# PLAN_tg_probe_13_account_construction
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_executable_probe_results.md
Status: [DONE] BLOCKED - Probe 11 selected-source closure failed before a constructible account/session graph existed.

## Question
Can tg_cli construct the minimal domain/account/session graph without opening a real profile or activating non-selected accounts?

## Probe Design
1. Use a synthetic temporary workdir owned by the probe.
2. Do not read the desktop default profile or user tdata.
3. Construct only the minimum objects established by Probe 11.
4. Add lifecycle checkpoints to stdout and exit before network/auth mutation.
5. Verify clean teardown and no replacement profile/account creation.

## Pass Criteria
- Minimal objects initialize and tear down cleanly.
- No real profile access.
- No unintended account/session activation.

## Fail Criteria
- Construction requires full desktop application/window state.
- Storage startup mutates synthetic state before validity checks.
- Object graph cannot be bounded without protected edits.

## Validation
- Run against a new temporary directory.
- Verify directory contents before/after and process exit code.
