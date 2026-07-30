# PLAN_tg_probe_18_a0_1_fence_corrections
Parent: PLAN_tg_probe_14_core_extraction.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Completed and validated on tg-cli **high**

## Goal
Make fence enforcement truthful for the complete tg-cli branch before any protected-source injection.

## Allowed Files
- Telegram/tg_cli/tools/check_tg_change_fences.py
- AGENTS.md
- Telegram/build/prepare/prepare.py
- TG_CHANGE_POLICY.md
- this plan and parent plan status/devlog

No other code changes.

## Tasks
1. Change hunk parsing to record a new-file anchor for every deleted line (the current new-file line counter at the deletion). Validate every deletion anchor, even in mixed hunks, against the resulting named fence block. Do not use only the hunk `new_start` as the deletion check.
2. Add self-tests:
- mixed hunk with fenced additions and deletion outside the block: fail
- valid fenced replacement: pass
- adjacent named blocks: pass
- duplicate marker IDs: fail
3. Fence the TG-specific AGENTS.md guidance:
```markdown
<!-- TG_CHANGE_BEGIN: tg-cli-agent-guidance -->
...
<!-- TG_CHANGE_END: tg-cli-agent-guidance -->
```
4. Fence the entire Python `stage('dav1d', ...)` expression from outside the triple-quoted command, so marker text is not emitted into command.bat:
```python
# TG_CHANGE_BEGIN: dav1d-github-mirror
stage('dav1d', """
    git clone -b 1.5.3 https://github.com/videolan/dav1d.git
    ...
""")
# TG_CHANGE_END: dav1d-github-mirror
```
5. Ensure Python triple-quoted command content remains byte-for-byte unchanged except the already-committed URL. Validate Python parsing and inspect/print the generated dav1d command without running the heavy build.
6. Update policy text and remove obsolete historical exception.

## Validation
1. `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
2. Checker against base `12e8d4a956` must pass with no allowlist.
3. `python -m py_compile Telegram/build/prepare/prepare.py`.
4. Use the prepare script's existing print path for only the dav1d stage in the validated Native Tools environment; select print/quit, never rebuild.
5. `git diff --check`.
6. Confirm only allowed files changed.

## Pass Criteria
- Full branch checker PASS against `12e8d4a956`.
- Mixed-hunk exploit is covered by a failing self-test.
- Each deleted-line anchor is checked, not just each deletion-containing hunk.
- dav1d command remains syntactically/semantically unchanged except URL.
- No checker exemptions or skipped existing files are added.

## Stop Conditions
- Correct fencing requires changing the dav1d stage behavior.
- Checker cannot distinguish mixed replacement safely without parsing old/new ranges; stop and redesign rather than weakening policy.

## Commit
Separate commit from A2.1 and A3. Suggested subject: `Tighten TG change fence enforcement`.

## Results
- Checker hunk parsing now records deletion anchors per deleted line (`deletion_anchors`) and validates each anchor against fenced replacement overlap.
- Added self-tests for:
  - mixed add/delete hunk with deletion anchor outside fence (fail path)
  - valid fenced replacement overlap (pass path)
  - adjacent named fence blocks (pass path)
  - duplicate marker IDs (fail path)
- Added fenced TG agent guidance block in AGENTS.md using `tg-cli-agent-guidance`.
- Fenced full dav1d stage wrapper in prepare.py using `dav1d-github-mirror`, with marker lines outside the triple-quoted command payload.
- Updated policy wording to require deletion checks for every hunk containing deletions and removed obsolete historical dav1d exception text.

## Fence IDs Touched
- `tg-cli-agent-guidance` (AGENTS.md)
- `dav1d-github-mirror` (Telegram/build/prepare/prepare.py)

## Validation Evidence
1. `python Telegram/tg_cli/tools/check_tg_change_fences.py --self-test`
    - Result: `SELFTEST PASS: valid and invalid fence cases behaved as expected.`
2. `python Telegram/tg_cli/tools/check_tg_change_fences.py --repo C:/work/git/tdesktop/tdesktop --base 12e8d4a956`
    - Result: `PASS: no fence violations found.`
3. `python -m py_compile Telegram/build/prepare/prepare.py`
    - Result: success (no output).
4. Prepare print-path for dav1d stage only:
    - Command: `$env:Platform='x64'; python Telegram/build/prepare/prepare.py dav1d`
    - Prompt action: `p` (print), then quit without rebuild.
    - Result: printed dav1d command block; command content unchanged except existing GitHub URL.
5. `git diff --check`
    - Result: success (no output).
6. Allowed-files scope check
    - Result: packet edits restricted to listed allowed files and plan docs.
