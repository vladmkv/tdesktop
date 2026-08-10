# NOTE_tg_probe_31_acceptance_report
Packet: PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md
Date: 2026-08-08 (local)
Status: implementation and Definition of Done validation complete; ready for explicit user acceptance. No commit performed.

## Commands And Exit Codes
1. Lightweight validation:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test` -> `0`
   - legacy `validate` against `12e8d4a956` -> `0`
   - explicit `validate` against `12e8d4a956` -> `0`
   - `quality` with locked packet-31 thresholds -> `0`
   - `inventory --check` -> `0`
2. Local no-fetch rehearsals:
   - `python Telegram/tg_cli/tools/check_tg_change_fences.py rehearsal --repo . --manifest TG_PROBES/tg_change_fence_manifest.json --config TG_PROBES/tg_change_rehearsal_config.json --mode merge --target upstream/dev --no-fetch --json-report TG_PROBES/reports/rehearsal_local_merge.json` -> `0`
   - same command with `--mode rebase` and report `rehearsal_local_rebase.json` -> `0`
3. Explicit-fetch parser/input-policy negative:
   - `rehearsal --mode rebase --no-fetch --fetch upstream dev` -> `2`; no network attempted.
4. Full integration:
   - `Telegram/tg_cli/tools/test_tg_change_fence_quality_packet31.ps1` -> PASS, reusing both fresh green rehearsal reports.
5. Supporting checks:
   - packet-26 hosted checkpoint review -> PASS.
   - packet-30 hosted account enumeration -> PASS.
   - `git diff --check` -> `0`.

## Report Summary
1. `fence_validate.json`: exit `0`; 87 checked files; 0 violations.
2. `fence_quality.json`: exit `0`; 0 violations; 12 warnings (`fence-size` and `change-density`).
3. `fence_inventory_check.json`: exit `0`; drift count 0.
4. `rehearsal_local_merge.json`: exit `0`; semantic mode `merge`; 24 phases; 0 failed phases.
5. `rehearsal_local_rebase.json`: exit `0`; semantic mode `rebase`; 27 phases; 0 failed phases.
6. `rehearsal_fetch_rebase_parser_negative.json`: exit `2` as required.

## Rehearsal Coverage
Both merge and rebase rehearsals completed these required phases:
1. Synthetic commit from the complete current worktree using a temporary index.
2. Disposable sibling worktree and local `upstream/dev` operation without fetch.
3. Manifest span overlay refresh, fence validation, quality, and inventory.
4. Local no-fetch submodule initialization and verification.
5. Fresh isolated configure under `${BUILD}`.
6. `tg_cli` build and help invocation.
7. Full Telegram Debug build using the stable single-compiler profile.
8. Packet-26 and packet-30 tests against `${BUILD}/Debug/tg.exe`.
9. Worktree removal and primary repository integrity verification.

The rebase rehearsal completed the native Git rebase path; fallback was not needed in the accepted run.

## Cleanup And Integrity
1. Packet-26 canonicalizes its configured binary path, and zero processes remain for the tested binary.
2. Rehearsal cleanup verifies Git deregistration and physical directory removal. A standard-library Windows long-path/read-only fallback handles partial `git worktree remove` failures.
3. After both accepted rehearsals:
   - temporary rehearsal branches: 0,
   - registered rehearsal worktrees: 0,
   - sibling `.tg-rehearsal-*` directories: 0,
   - processes running from rehearsal directories: 0.
4. Primary HEAD remained `d6c9060c5abbd2b2ec439697ea649c324fa20faf`; HEAD, index tree, and status were unchanged across each rehearsal.
5. Report scans found no absolute local path, user name, persistent profile path, or sensitive cache value.

## Fence Quality Changes
1. Split `storage-domain-owner-account-factory` into legacy allocation, modern loop, and scratch fences.
2. Split broad launcher profile gates by initialization, owner-probe, and snapshot-validation concerns.
3. Narrowed the dav1d mirror, account proxy, and domain close-window fences to their semantic deltas.
4. Retained the approved storage-read classifier exception with manifest rationale and review timestamp.

## Remaining Warnings And Disposition
1. Twelve quality warnings remain below failure thresholds. They are tracked fence-size or density warnings, not validation failures.
2. Explicit network fetch execution remains intentionally untested; parser and mutual-exclusion policy are validated without network access.
3. Packet 31 is ready for user acceptance. No commit has been made, and A10 remains blocked until acceptance and commit.