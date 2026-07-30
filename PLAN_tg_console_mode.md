# PLAN_tg_console_mode
Companion note: NOTE_tg_console_mode.md

## Locked Constraints
- Existing upstream files may be changed only by the approved fallback-A extraction plan and only inside named TG_CHANGE fences defined in TG_CHANGE_POLICY.md.
- All new implementation work goes into tg_cli target and new tg_cli-specific files.
- Branch must remain runnable after each stage (no "broken intermediate" states).
- tg desktop one-time auth bootstrap is the default early-path for session initialization.
- Existing desktop APIs and call sites remain source-compatible through additive overloads; broad constructor/signature rewrites are forbidden.
- tg and tg_cli never access the shared profile concurrently.
- Early tg_cli stages may retain lib_ui/QtGui/QtWidgets; stripping is incremental.

## Locked Scope
- Windows x64 Debug first.
- Interactive REPL plus one-shot commands.
- One selected account per process.
- Private chats plus channels/supergroups.
- Paged text history and media metadata only.
- Send text where permissions allow.
- Configurable mark-read behavior.
- Desktop default profile location with --workdir override.
- Secure no-echo local passcode prompt.
- BUILD_TG_CLI guarded CMake integration.
- Stable tg_cli chat IDs for command peer selection.
- Text output plus experimental --json until Stage 9 schema freeze.
- Explicit watch command for incoming updates.
- Mark-read enabled by default in tg_cli-owned per-profile config outside tdata.
- Edit own text messages and delete for everyone when permitted.
- Delete confirmation in REPL; --yes required for one-shot deletion.

## Invariant Checks (run at each stage end)
1. Build tg desktop target successfully.
2. Build tg_cli target successfully.
3. Confirm protected paths are unchanged (except approved docs):
- Existing lib_* paths remain unchanged.
- Existing Telegram/SourceFiles changes are limited to approved capability extraction files/blocks.
- Every modified upstream file passes the TG change fence checker.
4. Smoke-run both executables and capture exit behavior.

## Tasks
1. [TODO] Stage 0: Baseline freeze + branch contract (**high**)
- Create feature branch from known-good tg desktop commit.
- Record protected-file policy (no edits in existing libs/core).
- Acceptance test:
	- Build tg desktop target.
	- Run tg desktop executable and verify startup.
	- Capture baseline command transcript in Devlog.

2. [DONE] Stage 1: Introduced tg_cli skeleton target. BUILD_TG_CLI is guarded and OFF by default; Windows x64 Debug tg_cli builds as CUI with QtCore only, --help exits 0, and protected source/library paths are unchanged.
- Add BUILD_TG_CLI option, one guarded add_subdirectory(tg_cli), and new source directory only.
- Implement minimal QCoreApplication main() with banner and clean exit.
- Acceptance test:
	- tg_cli builds and runs.
	- tg desktop still builds and runs.
	- No changes outside tg_cli-owned files and CMake wiring.

3. [TODO] Stage 2: Desktop-source reuse feasibility spike (**high**)
- Execute granular plans:
	- TG_PROBES/PLAN_tg_probe_11_executable_closure.md
	- TG_PROBES/PLAN_tg_probe_12_executable_qt_runtime.md
	- TG_PROBES/PLAN_tg_probe_13_account_construction.md
- Compile selected Main/Storage/MTP sources into tg_cli without source edits.
- Determine exact transitive source/link closure and required Qt/UI modules.
- Probe QCoreApplication versus QApplication requirement.
- Probe account/session construction without opening a real profile.
- Acceptance test:
	- Minimal account/session initialization succeeds.
	- Protected paths remain unchanged.
	- Required source closure and dependencies are documented.
- Decision gate:
	- If selected-source reuse is not finite/maintainable, stop and select a fallback architecture before Stage 3.
	- Current result: STOP. Probe 11 failed after three bounded expansions with 153 unresolved externals; Probes 12/13 are blocked. Select a fallback architecture before continuing.
	- Selected planning direction: fallback A, four fenced capability seams. Implementation remains gated by TG_PROBES/PLAN_tg_probe_14_core_extraction.md.

### Next Implementor Queue
Execute in this exact order; do not combine commits:
1. A7 CLI bundle + synthetic Domain/Account/Session construction/runtime-closure packet (includes executable closure decision gate for `MTP::Instance`/`Core::App` coupling).
2. A8 profile ownership and workdir-resolution packet.
3. A9 passcode, account selection, and status packet.
4. A10 chats and paged-history packet.

Current readiness:
- A0.1 through A5 are complete and validated.
- A4 implementation commit: `530dec607a`.
- A5 implementation commit: `16babf664f`; A5 review-fix commit: `884cb247de`.
- A6 is complete and validated as desktop seam only (Domain lifecycle extraction + owner account-factory routing).
- A7 is now the active next packet for CLI bundle/factory wiring plus synthetic construction/runtime closure before any profile work.

### First Runnable Read-Only Version (V0)
V0 is reached after A10 and provides these one-shot commands over an existing desktop-authenticated profile:
- `tg_cli --workdir <path> status`
- `tg_cli --workdir <path> accounts`
- `tg_cli --workdir <path> --account <index> chats --limit <count>`
- `tg_cli --workdir <path> --account <index> read <chat-id> --limit <count>`

V0 requirements:
- tg desktop must be closed; tg_cli acquires and retains tg-compatible exclusive profile ownership before any tdata read.
- Missing profile, busy profile, wrong workdir, wrong passcode, corrupt profile, and missing authentication are distinct failures.
- Only the selected account starts a session.
- History is paged; text and media metadata are displayed without downloading media.
- Mark-read behavior is configurable and stored in tg_cli-owned configuration outside tdata.
- Human-readable output is required; `--json` remains experimental.
- Send, edit, delete, watch, and native CLI authentication are not part of V0.

4. [TODO] Stage 3: Safe shared-profile bootstrap (**high**)
- Define deterministic workflow: authenticate in tg desktop once, then fully close it.
- Resolve desktop default workdir with --workdir override.
- Add profile-in-use detection and refuse unsafe concurrent access.
- Prompt securely for local passcode when required.
- Enumerate accounts and select one for the process.
- Implement session-status diagnostics.
- Acceptance test:
	- Authenticated account opens from desktop profile.
	- Busy profile, missing auth, wrong workdir, and wrong passcode are distinct failures.
	- tg and tg_cli cannot mutate the shared profile concurrently.

5. [TODO] Stage 4: Read-only REPL and one-shot commands (**high**)
- Implement status, accounts, chats, read, more, and settings commands.
- Support private chats and channels/supergroups.
- Show media metadata without transfer.
- Implement configurable mark-read behavior.
- Acceptance test:
	- Paged history works for private chats and channels/supergroups.
	- REPL and one-shot paths return equivalent results.
	- Empty, permission, timeout, and network errors do not crash.

6. [TODO] Stage 5: Text sending (**high**)
- Implement send command for private chats and channels/supergroups where permitted.
- Define stable peer selector syntax for REPL and scripts.
- Add sent/fail/retry-needed feedback.
- Acceptance test:
	- Sent text appears in tg desktop for the same account.
	- Permission, invalid-peer, and network failures are handled without crash.

7. [TODO] Stage 6: Edit/delete own messages (**medium**)
- Implement edit for own text messages only.
- Implement delete-for-everyone where Telegram permits it.
- Prompt before delete in REPL; require --yes in one-shot mode.
- Acceptance test:
	- Edits/deletions appear in tg desktop.
	- Declined/missing confirmation causes no mutation.
	- Permission, time-window, and network failures are distinct.

8. [TODO] Stage 7: Incremental UI dependency stripping (**medium**)
- Capture baseline source/link/runtime dependency graph.
- Remove one desktop-only dependency cluster per iteration.
- Keep QtCore/QtNetwork as accepted runtime substrate.
- Add tg_cli-owned adapters/stubs only at verified UI boundaries.
- Re-run full Stage 3-6 smoke matrix after each reduction.
- Acceptance test:
	- Each reduction preserves profile/read/send behavior.
	- Protected upstream sources remain unchanged.
	- Final remaining lib_ui/QtGui/QtWidgets dependencies are documented.

9. [TODO] Stage 8: Optional native CLI auth (replace bootstrap dependency) (**medium**)
- Add interactive phone/code/2FA flow in terminal.
- Keep Stage 3 desktop-profile bootstrap as fallback mode.
- Acceptance test:
	- Fresh environment can authenticate from tg_cli without launching tg desktop.
	- Existing desktop-profile bootstrap path still works as fallback.

10. [TODO] Stage 9: Reliability hardening and release readiness (**medium**)
- Add help, structured logs, exit codes, reconnect behavior.
- Freeze/version the --json output schema.
- Finalize repeatable build/run instructions for both flavors.
- Acceptance test:
	- Full smoke matrix passes twice consecutively.
	- Operator runbook is complete and reproducible.

11. [TODO] Stage 10: No-Qt feasibility decision (separate future track) (**future**)
- Evaluate replacing Qt runtime substrate only after stable tg_cli exists.
- Produce an RFC if pursued; do not mix with primary tg_cli delivery.

## Devlog
- 2026-07-30: Performed deep static build/runtime dependency analysis.
- 2026-07-30: Found Qt is not only UI in this codebase; core + mtproto heavily depend on QObject/QThread/QNetwork/QCoreApplication behavior.
- 2026-07-30: Determined full Qt removal is a re-platforming effort; recommended phased approach is a parallel console target with retained QtCore/QtNetwork substrate.
- 2026-07-30: Created NOTE_tg_console_mode.md and this plan file as implementation guide.
- 2026-07-30: Locked execution strategy to stage-gated, always-runnable increments with explicit tg desktop auth bootstrap before tg_cli session reuse.
- 2026-07-30: Completed ten delegated static technical probes. Consolidated findings: TG_PROBES/NOTE_tg_probe_results.md.
- 2026-07-30: Static probes support continuing, but Gate B remains open: Main::Session source closure and Qt runtime choice require executable measurement.
- 2026-07-30: Locked canonical chat IDs to user<id>, chat<id>, channel<id>.
- 2026-07-30: Locked shared-profile ownership to fail-closed acquisition of tg-compatible QLocalServer before any tdata access.
- 2026-07-30: Executable Probe 11 failed the selected-source/no-protected-edit architecture after three attempts; 153 unresolved externals spanned Core/Main/Data/Storage/Window/UI. Gate B failed for this architecture; Probes 12/13 blocked.
- 2026-07-30: Designed fallback A using four capability interfaces (domain lifecycle, account network, session services, storage settings), additive overloads, and mandatory named TG_CHANGE fences with automated enforcement.
- 2026-07-31: A6 desktop seam packet completed and validated; Domain lifecycle capability routing and owner account-factory account construction are in place, with CLI domain bundle/runtime closure intentionally deferred to A7.

## Stage Command Matrix

Stage 0 validation commands:
- Build desktop target
- Run desktop executable

Stage 1 validation commands:
- Build tg_cli target
- Run tg_cli --help (or equivalent minimal command)
- Rebuild desktop target

Stage 2 validation commands:
- Build/run account/session initialization probe without opening a profile
- Capture source closure and dependent libraries

Stage 3 validation commands:
- Run tg desktop for login bootstrap once, then fully close it
- Run tg_cli session-status
- Verify concurrent profile use is rejected

Stage 4 validation commands:
- Run tg_cli status
- Run tg_cli chats
- Run tg_cli read <peer>
- Run tg_cli more
- Repeat representative commands in REPL
- Verify mark-read default and persisted toggle behavior
- Run representative one-shot commands with --json

Stage 5 validation commands:
- Run tg_cli send <peer> <text>
- Verify message visibility from tg desktop

Stage 6 validation commands:
- Run tg_cli edit <chat-id> <message-id> <text>
- Run tg_cli delete <chat-id> <message-id> and decline confirmation
- Run tg_cli delete <chat-id> <message-id> --yes

Stage 7 validation commands:
- Export tg_cli source/link/runtime dependencies before and after each reduction
- Run full status/chats/read/send/edit/delete smoke matrix after each reduction

Stage 8 validation commands:
- Run tg_cli login flow on fresh profile
- Verify post-login status/read/send

Stage 9 validation commands:
- Execute full smoke matrix twice
- Validate runbook from clean shell
- Validate versioned JSON fixtures

## Static Probe Outcomes
- Probe 01 source closure: CONDITIONAL.
- Probe 02 Qt application runtime: CONDITIONAL.
- Probe 03 profile ownership: CONDITIONAL; compatible IPC ownership algorithm identified.
- Probe 04 local passcode: CONDITIONAL; storage-level non-UI API identified.
- Probe 05 account selection: CONDITIONAL PASS.
- Probe 06 chat/history paging: CONDITIONAL PASS.
- Probe 07 text send: CONDITIONAL PASS.
- Probe 08 chat IDs: PASS.
- Probe 09 edit/delete: CONDITIONAL PASS.
- Probe 10 dependency floor: CONDITIONAL.

Detailed results: TG_PROBES/NOTE_tg_probe_results.md.

## Remaining Executable Probes
1. Measure actual selected-source compile/link closure for Main::Account/Main::Session/Data::Session.
2. Test QCoreApplication first and QApplication without windows second on Windows x64 Debug.
3. Construct minimal account/session objects without opening a real profile.
4. Verify non-selected accounts remain inactive.
5. Implement and test fail-closed QLocalServer profile ownership, including ambiguous IPC errors.
6. Verify wrong passcode/corrupt profile/wrong workdir never triggers start-from-scratch writes.
7. Verify media metadata extraction does not initiate downloads.
8. Verify server acknowledgment/error mapping for send/edit/delete.
9. Measure final dependency floor during Stage 7 cluster removal.
