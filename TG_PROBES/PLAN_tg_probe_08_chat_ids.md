# PLAN_tg_probe_08_chat_ids
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] PASS - canonical typed textual chat-ID grammar selected.

## Question
How should stable tg_cli chat IDs encode Telegram peer identity across restarts and upstream updates?

## Scope
- Users, basic groups, channels/supergroups.
- CLI text and JSON representations.

## Procedure
1. Inspect PeerId/UserId/ChatId/ChannelId representations and serialization helpers.
2. Determine stability and collision/type-encoding properties.
3. Compare raw numeric, typed textual, username, and opaque mapping approaches.
4. Define parse/format validation and lifecycle rules.
5. Ensure IDs contain no secrets and remain valid across sessions.

## Pass Criteria
- One canonical reversible typed ID maps unambiguously to existing peer identifiers without persistent side tables.

## Fail Criteria
- Existing IDs are unstable or require profile-specific mutable mapping.

## Output
Recommend grammar with examples, validation rules, JSON type, compatibility policy, and verdict.
