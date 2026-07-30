# PLAN_tg_probe_07_send_api
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL PASS; non-UI request path identified.

## Question
Can tg_cli send plain text to private chats/channels/supergroups without compose or window controllers?

## Scope
- Plain text only; permission and delivery/error handling.

## Procedure
1. Trace desktop plain-text send from UI boundary to API request.
2. Identify the lowest reusable send service/API wrapper.
3. List required peer/session/history state and random-id/idempotency behavior.
4. Trace success/failure/update acknowledgment.
5. Identify channel permission checks and server-authoritative errors.

## Pass Criteria
- A non-UI call path can send text and report definitive success/failure.

## Fail Criteria
- Send operation requires UI-owned compose/controller state or protected-source changes.

## Output
Document call sequence, required state, error taxonomy, idempotency requirements, and verdict.
