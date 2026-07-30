# PLAN_tg_probe_15_low_level_mtproto
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_fallback_architectures.md
Status: [TODO] Decision pending **high**

## Architecture
Build a tg_cli-owned account/chat/message model over td_mtproto/td_scheme and reusable low-level storage primitives, without protected-source edits.

## Locked Tradeoff
- Excellent upstream mergeability.
- Desktop tdata reuse requires duplicating proprietary storage parsing and is not proven safe/maintainable.
- Update synchronization, entity caches, and auth become tg_cli-owned protocol logic.

## Granular Probes
1. Synthetic-format probe: identify exact low-level source/target closure for encrypted-file reading without real tdata.
2. Read-only tdata import probe against a copied test fixture only; extract local key, auth keys, DC/config without Storage::Account.
3. Construct MTP::Instance with synthetic/imported fields and perform an offline lifecycle probe.
4. With explicit approval and test account, issue one harmless get-config request.
5. Implement getDialogs mapping to tg_cli DTOs; measure LOC and state requirements.
6. Implement getHistory paging without live update state.
7. Implement send-to-Saved-Messages acknowledgment mapping.
8. Decide whether minimal updates/getDifference implementation is bounded.

## Stop Conditions
- Storage parser duplicates more than 300 LOC before auth/config extraction.
- MTP::Instance requires Core/Main global state.
- One operation exceeds 600 LOC or requires Data::Session.
- Update correctness requires recreating the desktop state model.

## Pass Criteria
- No protected edits.
- Bounded read-only session import and one end-to-end operation.
- Estimated first usable scope remains below 4000 new LOC.

## Safety
- Never write desktop tdata.
- Use copied/synthetic fixture only until parser proof passes.
- No network/auth operation without explicit user approval.
