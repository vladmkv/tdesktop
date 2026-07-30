# PLAN_tg_probe_06_history_api
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL PASS; runtime depends on parent Stage 2 session construction.

## Question
Can tg_cli list chats and page message history without Window::SessionController or other UI-owned controllers?

## Scope
- Private chats, channels, supergroups; text and media metadata.
- No media download or mutation.

## Procedure
1. Trace desktop dialog-list population and history request paths.
2. Identify MTProto/API wrappers usable without window controllers.
3. Separate server request, Data::Session ingestion, and UI presentation layers.
4. Map pagination cursors/offsets and update handling.
5. Propose CLI DTOs for chat/message/media metadata.

## Pass Criteria
- A reusable or tg_cli-adaptable path exists for list/read paging without UI controller construction.

## Fail Criteria
- Required request/result processing is inseparable from UI-owned models without protected edits.

## Output
Document call sequence, source dependencies, paging semantics, DTO fields, and verdict.
