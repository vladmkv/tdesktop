# NOTE_tg_console_mode
Companion plan: PLAN_tg_console_mode.md

## Goal
Create a branch from the current working code and produce a console/text-mode Telegram client variant with basic messaging + reading features, while removing desktop UI dependencies where feasible.

## Locked Strategy (non-negotiable)

1. Preserve upstream mergeability:
- Existing core code and existing libs are treated as read-only for this effort.
- No edits in Telegram/lib_base, Telegram/lib_crl, Telegram/lib_storage, Telegram/lib_tl, Telegram/lib_ui, or existing Telegram/SourceFiles modules unless a formal exception is approved after a stage gate fails.
- Existing source files may be compiled into tg_cli where required; "read-only" means no source edits, not no source reuse.

2. Build both flavors from one codebase:
- Keep Telegram desktop target fully functional.
- Add a parallel target (tg_cli) with new source tree only (for example Telegram/tg_cli/...).

3. Testability at every stage:
- Each stage must finish with a runnable artifact and a short smoke test.
- No stage may leave the branch in "builds but cannot run" state.

4. Authentication bootstrap policy:
- Run tg desktop once for interactive auth/session bootstrap.
- tg_cli reuses the persisted session storage after that first auth.
- Native interactive auth inside tg_cli is deferred to a later stage.
- tg and tg_cli must never access the shared profile concurrently.
- tg_cli must refuse to open a profile when tg appears to be using it.

5. Ownership boundary policy:
- New code location is constrained to tg_cli-owned paths only.
- Preferred layout:
  - Telegram/tg_cli/main/
  - Telegram/tg_cli/app/
  - Telegram/tg_cli/commands/
  - Telegram/tg_cli/runtime/
  - Telegram/tg_cli/CMakeLists.txt
- Existing Telegram/SourceFiles and existing lib_* trees are integration dependencies, not edit targets.

6. Initial dependency policy:
- Early tg_cli stages may link lib_ui, QtGui, and QtWidgets when required by reused desktop session sources.
- Dependency removal is incremental and behavior-preserving, not a Stage 1 prerequisite.

## Locked Product Decisions
- Backend: reuse selected desktop account/session/data/history sources without modifying them.
- Runtime UX: interactive REPL plus one-shot commands backed by the same handlers.
- Initial platform: Windows x64 Debug.
- Profile: use desktop default profile-location logic with optional override.
- Profile passcode: secure no-echo console prompt; never command-line or environment storage.
- Account scope: select one account for each tg_cli process.
- Chat scope: private chats plus channels/supergroups.
- Channel behavior: read and send where account permissions allow.
- Message scope: text content plus media metadata; no media transfer initially.
- History: paged reads.
- Read receipts: configurable setting from the first usable version.
- CMake integration: one guarded add_subdirectory(tg_cli) in upstream CMake, controlled by BUILD_TG_CLI.
- Peer selector: chats emits a stable tg_cli chat ID used by read/send/edit/delete; username is optional convenience only.
- Output: human-readable text by default; one-shot commands support --json.
- JSON compatibility: experimental until Stage 9 hardening, then versioned/stable.
- Incoming updates: shown only after explicit watch command.
- Read receipts: enabled by default and persisted in a tg_cli-owned config file beside the selected desktop profile (outside tdata).
- Edit scope: own text messages only.
- Delete scope: delete for everyone when Telegram permissions/time rules allow.
- Destructive confirmation: prompt in REPL; one-shot delete requires --yes.

## What Was Analyzed

### 1. Top-level executable dependency graph
Evidence from Telegram target linkage in Telegram/CMakeLists.txt:
- Telegram links desktop-app::lib_ui directly.
- Telegram links desktop-app::external_qt and desktop-app::external_qt_static_plugins indirectly through linked libs.
- Telegram target enables AUTOMOC.

Implication:
- Current executable target is architected around Qt object model and UI stack, not only around protocol/networking.

### 2. Core runtime coupling to Qt (not just UI)
Evidence from SourceFiles/core and SourceFiles/mtproto:
- core/application.cpp includes QtGui/QGuiApplication and uses QCoreApplication, QObject event filtering, translators.
- core/sandbox.h defines Sandbox as QApplication subclass.
- core/sandbox.cpp uses QApplication notify/event loop behavior, QThread, QNetworkProxy, QMetaObject.
- core/launcher.cpp configures QApplication global attributes.
- mtproto/* uses QObject/QThread heavily and QtNetwork (QNetworkAccessManager, QNetworkReply, QNetworkRequest).

Implication:
- Qt in this codebase is a runtime substrate (event loop, thread model, networking, signal/slot) in core/mtproto, not only a rendering toolkit.

### 3. Library-level coupling
Evidence from CMake:
- lib_base links desktop-app::external_qt explicitly and contains many qthelp/qt integration files.
- lib_tl depends on lib_base.
- lib_storage depends on lib_base.
- lib_crl can use non-Qt backends on some configs, but Telegram app still relies on other Qt-bound modules.

Implication:
- Removing lib_ui alone does not produce a console-ready core. A large portion of non-UI code still depends on Qt via lib_base and mtproto.

## Feasibility Conclusion

### Can Qt be stripped down completely?
Short answer: not in a small change set.

Detailed answer:
- Full Qt removal would require replacing foundational services: event loop, object lifetime/signals, async networking, threads/timers, translation/localization integration, and app lifecycle.
- This is effectively a deep re-platforming of Telegram Desktop internals, not a normal feature branch task.

### Can UI be stripped while keeping Qt core?
Yes, this is the practical path.
- Keep QtCore/QtNetwork (and possibly a minimal QCoreApplication runtime).
- Remove QtWidgets/QtGui/UI module usage from the new console target where possible.
- Build a new executable entrypoint that exercises existing mtproto/session logic with text I/O.

## Multi-stage Migration Strategy (working software after each stage)

### Stage 0: Freeze baseline and branch contract
Objective:
- Freeze a known-good baseline commit where tg desktop builds and runs.

Deliverables:
- New feature branch.
- Documented contract: "existing libs and core modules remain untouched."

Pass criteria:
- tg desktop build passes and executable runs.

Failure response:
- If baseline is unstable, stop and re-baseline before any tg_cli work.

### Stage 1: Add empty tg_cli target that runs
Objective:
- Introduce tg_cli target with isolated new files only.

Implementation shape:
- New CMake entries that add tg_cli executable.
- Minimal main() using QCoreApplication for the skeleton; Stage 2 determines whether reused desktop sources later require QApplication.
- Print startup banner and exit cleanly.

Pass criteria:
- tg_cli builds and runs.
- tg desktop still builds and runs unchanged.
- tg_cli source tree contains only new files under tg_cli-owned paths.

Failure response:
- Revert tg_cli linkage until both targets are green.

### Stage 2: Technical feasibility probes
Objective:
- Prove the selected desktop-source reuse architecture before implementing commands.

Implementation shape:
- Compile selected Main/Storage/MTP sources into tg_cli without modifying them.
- Measure unresolved UI symbols and required source closure.
- Prove that a console-owned application/event loop can initialize the reused sources.
- Audit actual profile-use detection and storage safety.

Pass criteria:
- Minimal account/session objects initialize without opening a real profile.
- Required source/link closure is finite and documented.
- No protected upstream source edits are required.

Failure response:
- Stop before feature work and choose one fallback explicitly:
  - allow a small upstream reusable-core extraction seam
  - implement a CLI-owned high-level model over low-level MTProto
  - switch backend to TDLib and abandon desktop tdata reuse

### Stage 3: Session reuse bootstrap path (tg-first auth)
Objective:
- Open a pre-authenticated desktop profile safely after tg is fully closed.

Implementation shape:
- Resolve desktop default workdir using existing behavior, with --workdir override.
- Refuse startup when profile ownership cannot be acquired safely.
- Prompt without echo for local profile passcode when required.
- List discovered accounts and select one for the process.
- Provide session-status diagnostics without exposing key material.

Pass criteria:
- tg_cli opens the selected authenticated account.
- Missing auth, wrong passcode, busy profile, and wrong workdir produce distinct errors.
- Starting tg concurrently is blocked or detected before profile mutation.

Failure response:
- Keep tg_cli at diagnostic-only mode; do not weaken profile safety.

### Stage 4: Read-only CLI (list/read)
Objective:
- Implement safe read-only operations first.

Feature set:
- list dialogs/chats
- page through messages from selected peer
- optional watch mode for incoming updates in text form
- show media type/name/size metadata without downloading
- configurable mark-read behavior

Pass criteria:
- User can run command sequence: status -> chats -> read.
- tg desktop still unaffected.

Failure response:
- Keep command parser, disable broken command path behind feature flag in tg_cli only.

### Stage 5: Send message capability
Objective:
- Add basic send text message command.

Feature set:
- send <peer> <text>
- delivery/error reporting
- local command history and deterministic command exit status
- private-chat and channel/supergroup permission handling

Pass criteria:
- Sent message visible in tg desktop after refresh.
- Retry/error output is clear and non-crashing.

Failure response:
- Keep send path optional and runtime-toggleable until stable.

### Stage 6: Edit/delete own messages
Objective:
- Add narrowly scoped message mutation after text sending is stable.

Feature set:
- edit <chat-id> <message-id> <new-text> for own text messages
- delete <chat-id> <message-id> for everyone when allowed
- REPL confirmation prompt for delete
- mandatory --yes for one-shot delete

Pass criteria:
- Edit and delete results are visible in tg desktop.
- Permission/time-window failures are distinct from network failures.
- No mutation occurs when confirmation is declined or --yes is absent.

Failure response:
- Disable edit/delete commands without affecting read/send functionality.

### Stage 7: Incremental UI dependency stripping
Objective:
- Remove one UI dependency cluster at a time while preserving all CLI behavior.

Implementation shape:
- Record baseline source/link dependency graph.
- Remove dead desktop-only source clusters first.
- Add tg_cli-owned adapters/stubs only when they do not duplicate protocol/storage logic.
- Re-run the full read/send smoke matrix after every removal.

Pass criteria:
- tg_cli has a documented dependency reduction from the previous stage.
- Runtime behavior unchanged for implemented commands.
- Final exclusion of lib_ui/QtWidgets is a target, not assumed achievable in one step.

Failure response:
- If a hard dependency is discovered, document it in NOTE and isolate with a tg_cli adapter boundary; do not patch upstream libs.

### Stage 8: Optional native CLI auth (no tg bootstrap)
Objective:
- Make tg_cli fully independent for first-login flow.

Feature set:
- phone/code/2FA prompt loop in terminal.

Pass criteria:
- Fresh machine/user can auth from tg_cli directly.

Failure response:
- Keep Stage 3 desktop-profile bootstrap model as supported fallback.

### Stage 9: Harden and operationalize
Objective:
- Stabilize command UX and supportability.

Feature set:
- structured logs
- deterministic exit codes
- reconnect/retry policies
- concise help

Pass criteria:
- Repeatable smoke tests pass for tg and tg_cli.
- Branch is ready for long-lived rebases on upstream.

## Recommended Technical Strategy

### Strategy A (recommended): add a new console executable target while keeping existing Telegram intact
- Create a new target (example name: tg_cli) in parallel with Telegram.
- Prefer QCoreApplication, but allow QApplication if the Stage 2 source-reuse probe proves it necessary.
- Reuse selected core/session/mtproto modules.
- Allow required lib_ui/UI-heavy linkage initially, then remove dependency clusters incrementally in Stage 6.
- Implement minimal command loop for:
  - auth/login
  - dialog list/read
  - sending text messages

Why this is best:
- Preserves current working desktop build.
- Isolates risk.
- Allows incremental progress and measurable milestones.

### Strategy B: fork and remove UI from existing Telegram target
- High risk and large blast radius.
- Breaks the known working build repeatedly.
- Harder to bisect and validate.

Recommendation: avoid Strategy B initially.

## Stage Gate Rules

- Gate A (after Stage 1): both tg and tg_cli run.
- Gate B (after Stage 2): selected desktop-source reuse architecture is proven feasible.
- Gate C (after Stage 3): shared profile ownership and authenticated session reuse are safe.
- Gate D (after Stage 4): read-only CLI is reliable before sending is enabled.
- Gate E (after Stage 5): text sending and permission/error handling are reliable before edit/delete is enabled.
- Gate F (after Stage 6): edit/delete safety and permission handling are reliable.
- Gate G (after Stage 7): mergeability posture still holds and each dependency reduction preserved behavior.
- Gate H (after Stage 9): freeze commands and versioned JSON schema; document reproducible build/run matrix.

No gate pass means no advance to next stage.

## Integration Architecture (target-state for Stage 3-7)

tg_cli process layers:
- CLI Shell layer:
  - parses commands and prints output
  - no protocol/storage business logic
- tg_cli Application layer:
  - orchestrates lifecycle and command dispatch
  - maps command requests to existing Telegram internals
- Adapter layer:
  - thin wrappers that translate between CLI-friendly DTOs and existing internal structures
  - single choke point for upstream API drift
- Existing Telegram internals:
  - session, mtproto, storage, auth primitives consumed as-is

Why this matters:
- Minimizes rebase pain by concentrating adaptation in a small, tg_cli-owned surface.
- Avoids scattering CLI conditionals through upstream files.

## High-Risk Areas
- Authentication flows currently tied to GUI-driven steps.
- Event-driven updates currently surfaced through UI models/controllers.
- Implicit dependencies on QApplication behavior in core startup path.
- Local storage/session startup assumptions that may expect full app environment.
- Build-graph assumptions where lib_ui side effects are expected by other modules.
- High-level Main/Data/History sources are compiled directly into the Telegram target, not exposed as a clean reusable library.
- Main/Data/History implementations directly reference UI/window/history-view code, so selected-source reuse may require a large transitive source closure.
- Storage uses atomic QSaveFile writes but no independent storage-layer interprocess lock was found; shared-profile exclusion must be enforced by tg_cli.
- Desktop default workdir discovery must be reproduced without silently opening a different profile.

## Technical Probe Status
1. Main/Data/History source closure: statically bounded but CONDITIONAL; executable CMake closure measurement remains.
2. QCoreApplication versus QApplication: CONDITIONAL; executable Windows runtime probe remains.
3. UI dependency floor: staged removal order defined; final floor remains runtime-measured.
4. Shared profile ownership: fail-closed tg-compatible QLocalServer algorithm identified; runtime race tests remain.
5. Local profile passcode: non-UI storage API identified; secure prompt/no-write behavior remains to validate.
6. Account selection: early enumeration path identified; selected-only startup remains to validate.
7. Chat/history paging: non-window-controller API path identified; runtime session integration remains.
8. Text send: non-UI request path identified; acknowledgment/error integration remains.
9. Stable chat IDs: resolved as user<id>, chat<id>, channel<id>.
10. Edit/delete: non-UI request paths and permission checks identified; runtime acknowledgment/error integration remains.

Static probe plans and consolidated outcomes are stored under TG_PROBES/. See TG_PROBES/NOTE_tg_probe_results.md for evidence, verdicts, and corrections.

## Validation Targets (for the future implementation branch)
- Can start process and initialize the minimum Qt application runtime proven necessary by Stage 2.
- Can connect to Telegram API using provided API ID/hash.
- Can complete login and persist session.
- Can fetch and print chats/messages.
- Can send text message to a selected peer.
- Existing Telegram desktop target still builds and runs unchanged.

## Scope Boundaries
- This effort is not a clean-room rewrite of Telegram core.
- This effort is not "no-Qt" initially.
- Initial success is a minimal text-mode client using existing internals with reduced UI dependencies.
- Protected-file policy is part of scope: prefer new tg_cli files and target wiring over edits to existing libs/core.

## Architecture And Packet Report (2026-08-02)

This section supersedes the original no-intrusion assumption where later executable probes disproved it. Detailed commands, fence IDs, and validation evidence remain in the individual packet files under `TG_PROBES/`.

### Executive Answer: Facade Or Rewrite?

This is a facade and hosted-runtime adaptation, not a rewrite of Telegram Desktop.

The initial design was a parallel `tg_cli` executable that compiled selected Telegram sources without changing them. Packet 11 proved that design did not have a bounded link closure: after three controlled expansions it still had 153 unresolved symbols across Core, Main, Data, Storage, Window, and UI. We therefore approved minimal protected edits rather than reimplementing MTProto, account/session state, storage formats, updates, histories, permissions, or message operations.

The approved protected edits create four dependency-injection seams:
1. `AccountNetworkCapabilities` isolates fallback configuration and proxy services.
2. `StorageSettingsCapabilities` isolates theme and application-settings services used by storage.
3. `SessionServiceCapabilities` isolates setup-email, download-manager, and optional-window services.
4. `DomainLifecycleCapabilities` plus the account factory isolates application lifecycle and per-account capability selection.

Telegram's existing implementations remain the backend. Existing desktop constructors and call sites are preserved through additive overloads, and desktop capability implementations forward to the original behavior. CLI/hosted implementations replace presentation-only operations with no-op/null behavior. All modifications to existing upstream files are enclosed by named `TG_CHANGE` fences and checked automatically.

The standalone QtCore-only `tg_cli` target still exists as a shell/staging target, but it cannot yet host the full Telegram account/session graph. The first working backend checkpoint therefore uses `tg -console`: a hosted console mode inside the already-linked Telegram runtime with window, tray, and media presentation suppressed. This is temporary architecture for reaching a useful CLI safely; dependency stripping and a possible separate full CLI executable remain later work.

### Quantified Change Footprint

Committed branch comparison:
- Base: `12e8d4a956b0e73c1f738112870a32ea079fa05f`.
- Measured head: `e10d91b0791ce0be7d1911995c1598c4e921e71e`.
- Documentation and plans: 4,445 added lines.
- TG-owned tests and tools: 1,131 added lines.
- TG-owned runtime code: 1,084 added lines.
- Existing upstream files: 609 added lines and 71 deleted lines.

Therefore, the apparent 7,000+ line branch size is mostly planning, probe evidence, fence enforcement, and tests. The committed production edit surface in existing upstream files is about 680 changed lines, including fence markers and compatibility overloads. No `lib_*` implementation was modified.

Current uncommitted A8.1 work is separate from those committed metrics:
- Tracked worktree: 259 additions and 12 deletions across 9 files.
- Untracked plans, TG-owned runtime helpers, demo scripts, and tests: approximately 1,551 lines across 12 files.
- These changes are in-progress snapshot diagnostics and are not part of the committed architecture baseline yet.

### Packet History: What And Why

1. **Packet 01 - source closure:** mapped the Main/Data/History source set to test whether unchanged selected-source reuse could be bounded; static result was conditional.
2. **Packet 02 - Qt runtime:** examined `QCoreApplication` versus GUI application requirements because a real console target should avoid UI runtime where possible; executable proof remained open.
3. **Packet 03 - profile ownership:** traced desktop single-instance behavior because concurrent access to `tdata` is unsafe; identified a fail-closed local-server ownership direction.
4. **Packet 04 - passcode:** found the non-UI local-passcode storage path because bootstrap must unlock profiles without GUI widgets; runtime no-write behavior remained to prove.
5. **Packet 05 - account selection:** located early account metadata because the CLI should start only one selected account; conditional pass.
6. **Packet 06 - history API:** located non-window chat-list and paged-history APIs because read-only commands are the first useful product milestone; conditional on session construction.
7. **Packet 07 - send API:** located non-UI text-send requests because sending follows read-only stability; conditional on the session graph.
8. **Packet 08 - chat IDs:** selected stable typed IDs (`user<id>`, `chat<id>`, `channel<id>`) so scripts can target peers consistently; passed.
9. **Packet 09 - edit/delete:** located non-UI mutation and permission paths because later commands must preserve Telegram authority rules; conditional pass.
10. **Packet 10 - dependency floor:** defined staged UI dependency removal because stripping everything before working behavior would be high risk; conditional roadmap produced.
11. **Packet 11 - executable closure:** compiled the selected source graph to test the no-intrusion hypothesis; failed after three bounded expansions with 153 unresolved symbols.
12. **Packet 12 - executable Qt runtime:** was blocked because Packet 11 never produced a constructible runtime whose application class could be measured.
13. **Packet 13 - account construction:** was blocked because Packet 11 never produced a bounded account/session graph.
14. **Packet 14 - fallback-A extraction:** became the parent plan for four narrow capability seams because no-edit selected-source reuse failed; A3-A7 execution is tracked beneath it.
15. **Packet 15 - low-level MTProto alternative:** recorded a possible independent backend because protected edits might have been rejected; remains an unselected high-cost fallback.
16. **Packet 16 - TDLib alternative:** recorded the fastest independent CLI backend option, but it cannot reuse Telegram Desktop `tdata` authentication; remains unselected.
17. **Packet 17 / A3 - account network seam:** injected fallback-config and proxy services into `Main::Account` while preserving desktop constructors; completed and validated.
18. **Packet 18 / A0.1 - fence enforcement:** fixed deletion/mixed-hunk checks and retrofitted branch fences because protected edits require enforceable merge discipline; completed and validated.
19. **Packet 19 / A2.1 - capability baseline:** corrected interface dependencies and completed desktop validation because injection could not proceed on an unverified baseline; completed.
20. **Packet 20 / A4 - storage settings seam:** passed settings/theme behavior from `Main::Account` into `Storage::Account` and added CLI no-op behavior; completed and validated.
21. **Packet 21 / A5 - session services seam:** made `Main::Account` own reusable session services and let recreated sessions borrow them; completed, validated, and lifetime-reviewed.
22. **Packet 22 / A6 - domain/account factory seam:** injected domain lifecycle behavior and routed all account creation through a fresh capability-bundle factory; completed and validated for desktop behavior.
23. **Packet 23 / A7 - standalone CLI bundle:** attempted to construct the CLI Domain graph, but bounded linkage again reached broad MTProto/Core dependencies; stopped after 3/3 attempts and rolled back.
24. **Packet 24 / A7.1 - minimal config target:** tested narrower MTProto-config partitions because Packet 23 failed; isolated compile probes disproved both bounded partition options.
25. **Packet 25 / A7.2 - backend decision:** compared TDLib, low-level MTProto, and hosted Telegram runtime; selected hosted runtime because it preserves desktop auth/storage and reaches a demo sooner.
26. **Packet 26 / A7.3 - hosted console checkpoint:** added `tg -console`, deterministic status output, no-window startup, and fail-closed disposable-workdir ownership; completed, reviewed, hardened, and validated.
27. **Packet 27 / A8 - live profile bootstrap plan:** designed diagnostics-only live-profile startup, but remains uncommitted and blocked because ownership identity must be safe before profile access.
28. **Packet 28 / A8.0 - ownership migration:** tested canonical/alias and mixed-version ownership identities because different path spellings can create dual owners; failed all three bounded migration options, so live-profile A8/A9 remain blocked.
29. **Packet 29 / A8.1 - isolated snapshot demo:** moved the next demo to a disposable copied profile because live ownership is unresolved; owner-probe semantics now distinguish absent/busy/ambiguous, while snapshot copy/classification gates remain in progress and uncommitted.

### How The Current Product Differs From Original Telegram Desktop

| Area | Original desktop | Current TG branch |
|---|---|---|
| Backend protocol and models | Telegram Desktop MTProto, Main, Data, History | Same implementations; not rewritten |
| Storage and authentication | Desktop `tdata` readers/writers | Same formats and storage code behind added safety/capability seams |
| Desktop UI | Always follows desktop startup/window paths | Unchanged by default; `-console` selects a no-window hosted path |
| Dependency access | Main/Storage call global `Core::App()` and theme/window services directly | Four narrow injectable capability interfaces at audited call sites |
| Account construction | Direct constructors in Domain/Storage | Owner factory creates fresh desktop or hosted capability bundles |
| Standalone CLI | None | Minimal QtCore-only `tg_cli` shell exists, but full backend is not linked into it |
| Working console backend | None | `tg -console` uses the full existing runtime without presentation windows |
| Profile safety | Desktop single-instance behavior | Additional fail-closed checkpoint/snapshot guards and ownership probes |
| Commands | GUI actions | User-facing `status/chats/read/send` are not implemented yet |

### Development Model From Here

The intended development surface is now clear:
1. Keep Telegram protocol, storage, account/session, data, history, and permission logic in original code.
2. Keep protected edits limited to narrow lifecycle/service seams and safety gates.
3. Add CLI orchestration, output DTOs, commands, snapshot tooling, and tests under `Telegram/tg_cli/`.
4. Use hosted `tg -console` until a separately linked backend becomes economically justified.
5. Strip UI dependencies only after `status`, `chats`, `read`, and `send` work reliably.

This means future CLI feature development should mostly occur in TG-owned files. Additional protected edits are acceptable only when a concrete original-code global or lifecycle assumption cannot be adapted through an existing seam. The project is not completely rewriting the app; it is exposing a controlled facade over the existing app and building a CLI application layer on top of that facade.
