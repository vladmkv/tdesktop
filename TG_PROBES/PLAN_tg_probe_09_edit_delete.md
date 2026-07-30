# PLAN_tg_probe_09_edit_delete
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_probe_results.md
Status: [DONE] Static analysis complete - CONDITIONAL PASS; non-UI request paths identified.

## Question
What non-UI API paths and permission rules support editing own text and deleting for everyone?

## Scope
- Own text messages only for edit.
- Delete for everyone where allowed.
- Private chats/channels/supergroups.

## Procedure
1. Trace desktop edit/delete from UI boundary to API requests.
2. Identify non-UI reusable services or direct MTProto calls.
3. Map ownership, permission, channel-role, and server time-window checks.
4. Distinguish local optimistic changes from server acknowledgment.
5. Define safe error taxonomy and confirmation inputs.

## Pass Criteria
- Non-UI request paths exist and server errors can be surfaced without inconsistent local state.

## Fail Criteria
- Operations require UI-owned controllers/models or unsafe local mutation before acknowledgment.

## Output
Document call paths, permissions, error semantics, safety rules, and verdict.
