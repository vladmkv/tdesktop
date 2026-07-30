# PLAN_tg_probe_04_passcode
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL; secure runtime prompt and no-write startup remain to validate.

## Question
Can tg_cli unlock a desktop profile with a secure console passcode without invoking UI code?

## Scope
- Local profile passcode only, not Telegram 2FA/network auth.
- Password input, key derivation, Storage::Domain/Main::Domain startup result handling.

## Procedure
1. Trace desktop passcode submission to storage startup.
2. Identify required byte encoding and retry/error semantics.
3. Determine whether passcode API is callable without UI controllers.
4. Define Windows no-echo input requirements and memory-lifetime rules.
5. Identify rate limiting or lockout behavior that tg_cli must preserve.

## Pass Criteria
- A tg_cli-owned no-echo reader can pass bytes to an existing non-UI storage API and distinguish wrong passcode safely.

## Fail Criteria
- Unlock logic is only reachable through protected UI implementation or exposes secrets unsafely.

## Output
Document callable symbols, input encoding, error model, security requirements, and verdict.
