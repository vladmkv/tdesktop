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
1. [DONE] Stage 0: Baseline freeze + branch contract. Branch `tg-cli`, base `12e8d4a956`, fence policy, and repeatable desktop validation were established.
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

3. [DONE] Stage 2: Desktop-source reuse feasibility spike. Result: FAIL for unchanged selected-source reuse; fallback-A capability seams and hosted runtime were selected and implemented.
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
1. A9: hosted dev-profile startup + existing account enumeration (`Main::Domain::start()` then `Main::Domain::accounts()`); no new account parser or account model. Packet 30 implementation/validation complete and user-accepted; local commit is part of this packet closeout. **high**
	- Description: cross from storage-only status into the existing Telegram Domain/Account/Session startup path and expose the account list already maintained by `Main::Domain`.
	- Definition of Done: dedicated dev profile starts with no windows; existing `accounts()` returns the authenticated account; deterministic text/JSON reports storage index and existing session identity; no duplicate parser/model; two runs match; desktop, packet-26, builds, fences, and diff checks pass.
2. A10.0: read-only API proof packet for existing session readiness, desktop-order dialog iteration, and `HistoryMessagesViewer` timeout/error semantics. **high**
	- Description: resolve the three remaining API choices with bounded executable/read-only evidence before writing chat/history feature code.
	- Definition of Done: exact session-ready signal and timeout are recorded; one existing dialog-list iteration path is proven to match desktop ordering; history viewer cancellation/error policy is selected; no feature implementation or duplicate request/model code is added; proof results are committed to the plan.
3. A10.1: chats command using existing Telegram dialog loading and list models. **high**
	- Description: request dialogs through `ApiWrap`, wait through `Data::Session`, iterate the A10.0-selected `Dialogs::MainList`, and format rows only.
	- Definition of Done: one command lists the first N real dev-profile chats with stable typed IDs, title/type/unread/pinned/date; order matches desktop; bounded timeout/cancel works; no windows, read receipts, downloads, custom sorting, or duplicate model; regressions/builds/fences pass.
4. A10.2: paged read command using `Data::HistoryMessagesViewer()` and existing message/media models. **high**
	- Description: resolve a stable chat ID to the existing `History`, consume bounded viewer pages, and format existing `HistoryItem` text and metadata without implementing MTProto history requests.
	- Definition of Done: one command reads deterministic bounded pages from a private chat and a channel/supergroup; pagination anchor works; media metadata causes no download; no read receipt; timeout/cancel/error exits cleanly; no `Window::Controller`; regressions/builds/fences pass.
5. A10.3a: shared command handlers for `accounts`, `chats`, `read`, `more`, `help`, and `quit`. **high**
	- Description: extract one command-dispatch layer used identically by one-shot invocation and the later interactive loop; handlers orchestrate A9/A10 adapters and contain no Telegram backend logic.
	- Definition of Done: commands have one parser/result contract, deterministic text plus experimental JSON where applicable, stable IDs and account selection flow through unchanged, and automated tests prove one-shot handlers produce the same results as direct A9/A10 calls.
6. A10.3b: interactive REPL loop over the shared handlers. **high**
	- Description: keep the hosted Telegram process/session alive, read terminal commands repeatedly, dispatch through A10.3a, print results, and exit cleanly on `quit`/EOF/Ctrl+C.
	- Definition of Done: a user can run `accounts`, `chats`, `read <chat-id> [limit]`, `more`, `help`, and `quit` in one process; malformed commands do not terminate the loop; per-command timeout/cancel works; terminal UTF-8 is correct; no windows/read receipts/downloads; one-shot/REPL parity, regressions, builds, fences, and clean teardown pass.
7. Live-profile ownership architecture (canonical/alias identity). **future**
	- Description: design a backward-compatible ownership identity so old/new binaries and equivalent path spellings cannot concurrently own one physical desktop profile.
	- Definition of Done: old-old, old-new, new-old, and new-new canonical/alias/case/junction matrix has exactly one owner per cell; no deadlock or pre-ownership profile write; ambiguous states fail closed; desktop startup compatibility is proven. Until then this track remains deferred and the dev profile is mandatory.

Current readiness:
- A0.1 through A6 are complete and validated (see individual implementation/review commits below).
- A7 standalone `tg_cli` backend construction was abandoned after repeated bounded-closure failures (packet 23/24); Option C (hosted `tg -console` mode inside the existing linked runtime) was selected instead (packet 25) and implemented (packet 26).
- A8.0 live-profile ownership-identity migration (canonical path vs. alias/junction) failed after 3 bounded attempts (packet 28) and remains unsolved.
- Scope decision (2026-08-02, path made persistent 2026-08-07): rather than solving live-profile sharing, development uses a dedicated isolated dev profile in the workspace sibling directory `../tg-dev-profile` (outside the Git repository), logged into once as a second device session. This sidesteps A8.0 entirely for development purposes; live-profile sharing is deferred indefinitely and no longer blocks progress.
- The packet-29 snapshot-copy approach (copy the live profile to a disposable directory, marker/manifest trust, robocopy) was implemented, found to have 3 high/5 medium defects on review (credential-cleanup-on-failure bug, forgeable marker trust, integrity check computed before the risk window, impure read path), and was abandoned in favor of the dev-profile approach above. All packet-29 snapshot-copy files were deleted; only its reusable read-only classifier survived (see below).
- A8 (profile status) is DONE against the dev profile: `tg.exe -console-profile-snapshot -workdir <profile> -console-log <path>` prints `snapshot-storage-status:ready|passcode-required|passcode-required-legacy|profile-corrupt|profile-not-found`, proven not to mutate `tdata`, with fail-closed argument guards. Commit `ba65a3d41b`.
- During A8 implementation, found and fixed three real runtime defects, not just packet-29 scope-splitting: (1) every console-mode launcher gate was dead code because flags were read before `Launcher::init()`/`processArguments()` ran; (2) the fail-closed abort path called `QCoreApplication::exit()` before any event loop existed and hung forever instead of terminating; (3) profile-status mode tripped checkpoint-lock enforcement meant only for `-console` checkpoint mode. All three are fixed and verified end to end (real `ready`, empty-dir `profile-not-found`, all four guards reject, `tdata` byte-identical before/after, packet-26 regression still passes).
- A9 packet 30 implementation and full Definition-of-Done validation are complete and user-accepted after running `run_hosted_console_accounts_demo.ps1` against the persistent dev profile. A10 has not started.
- A10 (chats/history) is not started.

### A9 Refined Packet: Reuse Existing Account List

Description:
- Start the existing hosted Telegram runtime against the dedicated dev profile and expose Telegram's already-populated account list. This is orchestration/output work, not account-list business logic.

Definition of Done:
- The A9 validation and stop-condition sections below are satisfied; the implementation contains no second account parser/model and is committed as one focused packet.

Decision:
- Do **not** implement another account-list parser. Telegram already has `Main::Domain::accounts()`, `orderedAccounts()`, and `accountsAuthedCount()`.
- Those APIs are runtime APIs: they are populated by `Storage::Domain::startModern()` through `Main::Domain::accountAddedInStorage()`. Therefore A9 must start the existing Domain/Account graph rather than duplicate its storage format.
- Normal startup may write upgrades/settings and starts MTP/session services. That is acceptable only because A9 targets the dedicated dev profile, not the desktop's live profile. A9 must not claim pure-read or live-profile safety.

Exact implementation scope:
1. Add one hosted `accounts` mode/command under `tg.exe -console`, reusing the existing hosted capability bundle.
2. Run the existing `Application::startDomain()` / `Main::Domain::start(QByteArray())` path with empty passcode.
3. Handle existing `Storage::StartResult`: success, passcode-required, passcode-required-legacy. Never call reset/forgotten-passcode or create a fresh account.
4. On success, read `Main::Domain::accounts()` and emit one deterministic record per entry: storage index, session existence, `Session::userId()`, `Session::uniqueId()`, test/production DC flag, and self-user display name/username if already available.
5. No new account DTO inside Main/Storage. A TG-owned formatter may copy fields into output JSON/text only.
6. Select one account by existing storage index for A10; default to existing active account. Do not invent a second ordering algorithm.

A9 validation:
- Dedicated dev profile only; desktop process closed.
- Output contains the authenticated dev account and matches the desktop account identity.
- Two runs produce the same account identity/order.
- No windows/tray/media presentation.
- Existing desktop startup and packet-26 demo remain unchanged.
- Build `Telegram` and `tg_cli`; fence checker and `git diff --check` pass.

A9 stop conditions:
- Existing `Domain::accounts()` cannot be populated without a window/controller.
- Hosted no-op capabilities prevent successful session creation.
- Startup falls into `startFromScratch()` or creates a new account/profile.
- Implementation duplicates account storage parsing or account model logic.

### A10 Refined Packet: Reuse Existing Chat And History Models

Description:
- Build `chats` and `read` as thin TG-owned orchestration/formatting adapters over the existing Telegram session, dialog list, history viewer, update ingestion, and media models.

Definition of Done:
- A10.0 proofs are committed first; A10.1 then lists real chats; A10.2 reads bounded history pages; A10.3a unifies command handlers; A10.3b adds the interactive REPL; all A10 validation and stop conditions below pass without duplicate MTProto/model code.

Locked reuse rules:
- Do not implement MTProto dialog/history requests in TG-owned code.
- Do not create parallel chat/message models.
- Do not require `Window::Controller`; the audited list/read APIs are model/session APIs.
- First demo never marks messages read and never downloads media.

A10.0 proof packet (must complete before feature code):
1. **Session-ready proof:** after A9 startup, prove the minimum existing readiness signal: active `Main::Session` plus updates bootstrap completion that triggers `ApiWrap::requestDialogs()`. Record the exact producer/callback and timeout behavior.
2. **Dialog-order proof:** prove which existing list matches desktop-visible ordering (pinned + indexed semantics versus `Dialogs::MainList::indexed()->all()` alone). Select one existing iteration path; do not merge/order rows in TG code.
3. **History-error proof:** trace `Data::HistoryMessagesViewer()` through `ApiWrap::requestHistory()`. Decide explicitly whether V0 accepts timeout/no-progress as its error contract or needs one small additive error callback seam. No implementation until this is decided.

A10.1 chats implementation after A10.0 passes:
1. Reuse `ApiWrap::requestDialogs(nullptr)` to start load.
2. Reuse `Data::Session::chatsListLoaded()` / `chatsListLoadedEvents()` for completion and `chatsListChanges()` for updates.
3. Iterate the selected existing `Dialogs::MainList` path and format first `N` rows only.
4. Stable IDs remain `user<id>`, `chat<id>`, `channel<id>` using existing `PeerId` type helpers.
5. Output fields: stable chat id, title, peer type/id, unread count, pinned state, last-message date. Formatting only; no duplicate model.

A10.2 paged read implementation after chats passes:
1. Resolve selected stable chat id to existing loaded `PeerData`/`History` through `Data::Session`.
2. Reuse `Data::HistoryMessagesViewer(history, around, before, after)` for paging and automatic request/model ingestion.
3. Format existing `HistoryItem` text and existing media metadata only. Never call load/download/save methods.
4. Default no-mark-read: never call `readInbox`, `readInboxTill`, or `sendPendingReadInbox`.
5. Bounded session/dialog/page timeouts and explicit cancellation/detach on exit.

A10.3a shared command handlers after read passes:
1. Define one TG-owned parser/dispatcher for `accounts`, `chats`, `read`, `more`, `help`, and `quit`.
2. Handlers call A9/A10 adapters only; they never call MTProto/storage/model internals directly.
3. One-shot and future REPL invocation use the same request/result objects and formatter.
4. Preserve selected storage index and per-chat pagination cursor in explicit command context.

A10.3b interactive REPL after handlers pass:
1. Start the hosted Domain/Session once and keep it alive until `quit`/EOF/Ctrl+C.
2. Read UTF-8 terminal lines, parse with A10.3a, dispatch asynchronously, and return to the prompt after success or recoverable error.
3. `more` continues the last successful `read` cursor; a new `read` replaces it.
4. Serialize commands: no overlapping account/dialog/history mutations in the first REPL version.
5. Clean exit cancels outstanding work, destroys command lifetimes, and tears down Domain/Session without a crash or leaked process.

A10 validation:
- List at least one real chat from the dedicated dev profile and compare identity/title/order with desktop.
- Read one bounded page twice with deterministic message ids/order.
- Private chat and channel/supergroup coverage.
- Media metadata shown without creating downloaded media files.
- No read receipt sent in the first demo.
- Timeout/network failure exits cleanly without windows or crash.
- One-shot and REPL commands produce equivalent account/chat/message records.
- A single interactive process executes `accounts -> chats -> read -> more -> help -> quit` successfully.

A10 stop conditions:
- Any selected list/read path requires `Window::Controller` construction.
- TG-owned code must duplicate MTProto request or Telegram chat/message model behavior.
- Existing history viewer cannot expose bounded progress/cancellation without an unbounded wait.
- First demo causes read receipts or media downloads.

### Deferred Live-Profile Ownership

Current identity is MD5 of `QDir(cWorkingDir()).absolutePath()`: it is path-string identity, not physical-directory identity. Identical spelling works; junction/symlink/case aliases can hash differently and permit two owners of one physical profile. Packet 28 tested three migration designs (global canonical identity, dual legacy+canonical identities, strict canonical mode plus profile-root lock); all failed the required old/new mixed-version matrix.

The dedicated dev profile sidesteps this because only the development build uses it; it does not fix production shared-profile ownership. Reopen only with a new migration design that passes old-old, old-new, new-old, new-new across canonical/alias/case/junction spellings with exactly one owner, no deadlock, no pre-ownership writes, and bounded fail-closed ambiguity. A profile-root lock alone is insufficient because older Telegram binaries do not participate in it.

### First Runnable Read-Only Version (V0)
V0 is reached after A10.3b and uses the hosted `tg.exe` backend (the separate `tg_cli.exe` remains a skeleton until a later packaging split). It provides both one-shot commands and an interactive prompt:
- `tg.exe -console-command accounts -workdir <dev-profile>`
- `tg.exe -console-command chats --limit <count> -workdir <dev-profile>`
- `tg.exe -console-command read <chat-id> --limit <count> -workdir <dev-profile>`
- `tg.exe -console-repl -workdir <dev-profile>`

Interactive V0:
```text
tg> accounts
tg> chats
tg> read user123 20
tg> more
tg> help
tg> quit
```

V0 requirements:
- tg desktop must be closed; tg_cli acquires and retains tg-compatible exclusive profile ownership before any tdata read.
- Missing profile, busy profile, wrong workdir, wrong passcode, corrupt profile, and missing authentication are distinct failures.
- Only the selected account starts a session.
- History is paged; text and media metadata are displayed without downloading media.
- Mark-read behavior is configurable and stored in tg_cli-owned configuration outside tdata.
- Human-readable output is required; `--json` remains experimental.
- Send, edit, delete, watch, and native CLI authentication are not part of V0.

4. [TODO] Stage 3: Safe profile bootstrap (**high**)
- Description: establish a repeatable authenticated profile workflow. Current implementation uses a dedicated dev profile; live desktop-profile sharing remains a deferred ownership track.
- Definition of Done: profile status and accounts commands handle ready/passcode/missing/corrupt states deterministically; dev-profile ownership rule is documented and enforced; no accidental start-from-scratch on invalid input; desktop and console regressions pass.
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
- Description: expose status, accounts, chats, and paged read through shared command handlers, reusing Telegram's models and network requests.
- Definition of Done: A9, A10.0, A10.1, A10.2, A10.3a, and A10.3b pass; one process supports the documented interactive loop; REPL and one-shot output are equivalent; stable IDs work; timeouts/errors do not crash; no read receipt or media download unless explicitly enabled.
- Implement status, accounts, chats, read, more, and settings commands.
- Support private chats and channels/supergroups.
- Show media metadata without transfer.
- Implement configurable mark-read behavior.
- Acceptance test:
	- Paged history works for private chats and channels/supergroups.
	- REPL and one-shot paths return equivalent results.
	- Empty, permission, timeout, and network errors do not crash.

6. [TODO] Stage 5: Text sending (**high**)
- Description: add a thin send command over Telegram's existing `ApiWrap`/`Data::Histories` send path after read-only behavior is stable.
- Definition of Done: private and permitted channel/supergroup sends receive server acknowledgment and appear in desktop; stable peer selectors are reused; permission/invalid-peer/network outcomes are distinct; no duplicate send protocol/model logic; full read-only regression passes.
- Implement send command for private chats and channels/supergroups where permitted.
- Define stable peer selector syntax for REPL and scripts.
- Add sent/fail/retry-needed feedback.
- Acceptance test:
	- Sent text appears in tg desktop for the same account.
	- Permission, invalid-peer, and network failures are handled without crash.

7. [TODO] Stage 6: Edit/delete own messages (**medium**)
- Description: expose existing edit and revoke/delete operations with Telegram's ownership, rights, and time-window checks.
- Definition of Done: own text can be edited; delete-for-everyone works only when allowed; REPL confirmation and one-shot `--yes` are enforced; decline/missing confirmation makes no mutation; permission/time/network errors are distinct; Stage 3–5 regression passes.
- Implement edit for own text messages only.
- Implement delete-for-everyone where Telegram permits it.
- Prompt before delete in REPL; require --yes in one-shot mode.
- Acceptance test:
	- Edits/deletions appear in tg desktop.
	- Declined/missing confirmation causes no mutation.
	- Permission, time-window, and network failures are distinct.

8. [TODO] Stage 7: Incremental UI dependency stripping (**medium**)
- Description: reduce hosted console compile/runtime UI dependencies one verified cluster at a time after useful commands work; preserve QtCore/QtNetwork as accepted substrate.
- Definition of Done: before/after dependency graph is recorded for each removal; status/chats/read/send/edit/delete matrix remains green; no broad upstream rewrite or duplicated backend logic; remaining QtGui/QtWidgets/lib_ui dependencies are explicitly justified.
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
- Description: provide terminal phone/code/2FA login so a fresh CLI profile can be created without launching the desktop UI, while retaining the dev-profile bootstrap fallback.
- Definition of Done: fresh isolated profile authenticates end-to-end with secure no-echo secrets and bounded retries; session persists and status/chats/read/send work after restart; existing profile path remains compatible; credentials never appear in arguments, environment, logs, or committed files.
- Add interactive phone/code/2FA flow in terminal.
- Keep Stage 3 desktop-profile bootstrap as fallback mode.
- Acceptance test:
	- Fresh environment can authenticate from tg_cli without launching tg desktop.
	- Existing desktop-profile bootstrap path still works as fallback.

10. [TODO] Stage 9: Reliability hardening and release readiness (**medium**)
- Description: stabilize command contracts, reconnect/timeout behavior, diagnostics, builds, and operator documentation for repeatable use.
- Definition of Done: versioned JSON schema and exit-code table are frozen; full smoke matrix passes twice from a clean shell; reconnect/cancel behavior is deterministic; build/run instructions reproduce both desktop and console modes; no known high-severity review findings remain.
- Add help, structured logs, exit codes, reconnect behavior.
- Freeze/version the --json output schema.
- Finalize repeatable build/run instructions for both flavors.
- Acceptance test:
	- Full smoke matrix passes twice consecutively.
	- Operator runbook is complete and reproducible.

11. [TODO] Stage 10: No-Qt feasibility decision (separate future track) (**future**)
- Description: reassess replacing Qt only after the CLI product is stable, using measured remaining dependencies rather than making no-Qt a delivery prerequisite.
- Definition of Done: an RFC inventories event loop/network/thread/timer/storage impacts, compares cost/benefit and alternatives, and records a go/no-go decision; no implementation is mixed into the primary CLI delivery without separate approval.
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
- 2026-07-31: A6 final implementation commit recorded as `d7daf02d01`; A6 review disposition is clean.
- 2026-07-31: A7 implementor packet authored at `TG_PROBES/PLAN_tg_probe_23_a7_cli_bundle_synthetic_construction.md` and queued as active next execution packet.
- 2026-07-31: A7 packet 23 resolved A7qq as Option A (stop after 3/3 bounded expansions). Failed/uncommitted A7 wiring was restored to `d7daf02d01`, tg_cli baseline was revalidated, and follow-on decision/probe work moved to packet 24 while keeping A8 closed.
- 2026-07-31: A7.1 packet 24 executed as bounded decision/probe. Read-only dependency graph and per-file dependency inventory were captured first; Option A and Option B were disproven by isolated compile closure evidence; Option C selected. Temporary probe wiring was rolled back and baseline tg_cli build/help plus fence checker and diff checks passed. Next queue item moved to A7.2 architecture decision packet 25.
- 2026-07-31: A7.3 packet 26 planning finalized as the sole ready implementor packet. Clarified TG-owned hosted sources under Telegram/tg_cli/hosted, Telegram-only hosted bundle wiring, strict synthetic empty-workdir-only validation, explicit H1 one-shot `-console-exit` gate before H2 persistent owner/single-instance mode, and mandatory desktop/tg_cli unchanged regressions.
- 2026-07-31: A7.3 packet 26 implemented and validated end-to-end. Option B chosen for 8q with no `Storage::Domain` invariant edits: hosted persistent empty-workdir mode now keeps only owner/event-loop/single-instance behavior, emits deterministic `console-ready`, passes H1/H2 and disposable-workdir non-console regression, and preserves tg_cli/desktop build regressions.
- 2026-07-31: A8 packet 27 was revised from completed packet-26 review findings into a diagnostics-only implementation-safe packet and gated by a new A8.0 ownership-identity migration probe. Queue now advances through A8.0 first; A9 remains closed.
- 2026-07-31: After A8.0 FAIL, packet 29 (`PLAN_tg_probe_29_a8_1_isolated_profile_snapshot_demo.md`) was authored as the sole ready next implementor packet for isolated snapshot-only diagnostics. Live-profile A8/A9 tracks remain blocked pending ownership architecture resolution.
- 2026-08-01: Packet 29 implementation resumed. One required Debug Telegram build completed successfully after cleanup, resolving the temporary no-space blocker. Runtime Gate G1 is still semantically blocked because owner probe reports `owner-ambiguous` for idle disposable source workdirs, and packet-29 synthetic gate stops before copy/classification.
- 2026-08-02: Independent review of the uncommitted packet-29 worktree found 3 high and 5 medium defects (source-integrity digest computed before the risk window; cleanup skipped on every failure exit path, leaving copied auth keys on disk; snapshot marker/manifest trust forgeable by self-consistent metadata; owner identity still alias/junction-ambiguous; classifier ran after `startLocalStorage()`/proxy init, violating the pure-read-path requirement). Two independently-complete artifacts (`run_hosted_console_demo.ps1`, packet-27 plan) were committed on their own (`ea23e4f15a`, `12d9c3a8ea`) after re-passing the packet-26 regression.
- 2026-08-02: Decision made to stop pursuing live desktop-profile sharing for development. Logged into a dedicated dev profile at `%TEMP%\tg-dev-profile` (outside the repo) as a second device session, sidestepping the unresolved A8.0 ownership-identity problem entirely rather than solving it.
- 2026-08-02: Implemented and validated profile status against the real dev profile (commit `ba65a3d41b`). `tg.exe -console-profile-snapshot -workdir <profile> -console-log <path>` reports `snapshot-storage-status:ready` for the authenticated dev profile and `profile-not-found` for an empty directory, with `tdata` proven byte-identical before/after. Found and fixed three real defects surfaced only by testing against real data: (1) console-mode launcher gates were dead code, evaluated before `processArguments()` populated the flags they read; (2) the fail-closed abort called `QCoreApplication::exit()` before any event loop existed and hung instead of terminating; (3) profile-status mode was tripping `-console` checkpoint-lock enforcement not meant for it. Deleted the now-dead packet-29 snapshot-copy machinery (marker/manifest guard, robocopy tool, its tests) since the dev-profile approach removed the need for it; kept only the reusable read-only storage classifier (`Domain::classifySnapshotStorage`) and owner-probe helper.

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
