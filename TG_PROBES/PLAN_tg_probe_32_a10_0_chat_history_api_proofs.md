# PLAN_tg_probe_32_a10_0_chat_history_api_proofs
Parent: PLAN_tg_probe_14_core_extraction.md
Previous packet: PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md
Companion roadmap: ../PLAN_tg_console_mode.md
Policy: ../TG_CHANGE_POLICY.md
Status: [DONE] Superseded on 2026-08-10. The source-backed readiness, order, and history no-progress decisions were folded into A10.1/A10.2 to remove a documentation-only gate; no separate proof execution is required. **high**

## Purpose

Close A10.0 by recording three exact API decisions needed by A10.1 chats and A10.2 read:

1. selected-account session and top-level dialog-list readiness;
2. the existing list iteration path that exactly matches the desktop default order;
3. `Data::HistoryMessagesViewer()` success, empty, failure, timeout, and cancellation semantics.

This packet was retained as planning history only. Its selected decisions are recorded in `../PLAN_tg_console_mode.md` and implemented with A10.1/A10.2, where runtime behavior can be validated.

## Inputs

Read these files and symbols only, plus at most one immediate caller/callee when a listed conclusion cannot be verified:

1. Existing hosted session acquisition:
   - `Telegram/tg_cli/hosted/hosted_console_accounts_mode.cpp`
   - `WaitForAnySession()`
   - `Main::Account::sessionValue()` / `sessionExists()`
2. Update/bootstrap to dialog request:
   - `Telegram/SourceFiles/api/api_updates.cpp`
   - `Api::Updates::stateDone()`
   - `Telegram/SourceFiles/apiwrap.cpp`
   - `ApiWrap::requestDialogs()`, `requestMoreDialogs()`, `requestPinnedDialogs()`, `dialogsLoadFinish()`
3. Dialog readiness and list ownership:
   - `Telegram/SourceFiles/data/data_session.h/.cpp`
   - `Data::Session::chatsListLoaded()`, `chatsListLoadedEvents()`, `chatsListDone()`, `chatsList()`
4. Desktop ordering:
   - `Telegram/SourceFiles/dialogs/dialogs_main_list.cpp`
   - `Dialogs::MainList::MainList()`, `indexed()`
   - `Telegram/SourceFiles/dialogs/dialogs_indexed_list.h/.cpp`
   - `Dialogs::IndexedList::all()`, `adjustByDate()`
   - `Telegram/SourceFiles/dialogs/dialogs_list.cpp`
   - `Dialogs::List::adjustByDate()`, `sortByDate()`
   - `Telegram/SourceFiles/dialogs/dialogs_row.cpp`
   - `Dialogs::Row::sortKey()`
   - `Telegram/SourceFiles/dialogs/dialogs_inner_widget.cpp`
   - constructor assignment to `_shownList` and default painting of `_shownList->all()`
5. History viewer/error behavior:
   - `Telegram/SourceFiles/data/data_history_messages.h/.cpp`
   - `Data::HistoryViewer()`, `HistoryMergedViewer()`, `HistoryMessagesViewer()`
   - `Telegram/SourceFiles/apiwrap.cpp`
   - `ApiWrap::requestHistory()`
   - `Telegram/SourceFiles/data/data_sparse_ids.h/.cpp`
   - `SparseIdsSliceBuilder::applyInitial()`, `applyUpdate()`, `mergeSliceData()`
   - `Telegram/SourceFiles/storage/storage_sparse_ids_list.cpp`
   - `Storage::SparseIdsList::addSlice()` and update emission
   - `Telegram/lib_rpl/rpl/lifetime.h`
   - `rpl::lifetime::~lifetime()` and `destroy()`

Do not map broad adjacent systems. If one conclusion remains unprovable from this set, stop and record that exact missing boundary.

## Locked Decision 1: Session And Dialog Readiness

### Selected session

Reuse A9's selected storage index and account/session lifecycle. A10.1 must not create another account selector.

Readiness sequence:

1. Resolve the selected `Main::Account` from the A9-selected storage index.
2. If `account->sessionExists()` is true, use `&account->session()` immediately.
3. Otherwise subscribe to `account->sessionValue()`, filter non-null, and take one value.
4. Use a 15,000 ms single-shot timeout, matching the existing bounded A9 session wait.
5. Destroy the RPL lifetime and stop the timer on success, timeout, or cancellation.
6. Timeout result token for future A10 implementation: `session-timeout`.

### Dialog load

After obtaining `Main::Session`:

1. Call `session.api().requestDialogs(nullptr)` explicitly. Do not rely only on bootstrap timing. Existing request-state guards make duplicate/in-flight calls bounded.
2. Readiness is top-level only (`folder == nullptr`).
3. If `session.data().chatsListLoaded(nullptr)` is already true, continue immediately.
4. Otherwise subscribe to `session.data().chatsListLoadedEvents()`, filter `folder == nullptr`, and take one event.
5. Call `requestDialogs(nullptr)`, then start the 30,000 ms single-shot timer unconditionally as soon as that call returns. The timer measures no readiness progress from the caller's explicit request point even when request-state guards reused an already in-flight bootstrap request; it does not depend on whether this call sent a new RPC.
6. Destroy the RPL lifetime and stop the timer on success, timeout, or cancellation.
7. Timeout result token for future A10 implementation: `dialogs-timeout`.

Meaning of ready:

`ApiWrap::dialogsLoadFinish(nullptr)` calls `Data::Session::chatsListDone(nullptr)` only after both normal dialog list data (`listReceived`) and pinned dialogs (`pinnedReceived`) are complete. Therefore `chatsListLoaded(nullptr)` is the selected V0 readiness signal; `chatsListChanged()` is not readiness.

Do not add polling, sleeps, retries, a second request implementation, or a capability seam in A10.0.

## Locked Decision 2: Desktop Dialog Order

A10.1 must iterate exactly:

```cpp
session.data().chatsList(nullptr)->indexed()->all()
```

Iteration is `cbegin()` to `cend()` with no transformation.

Rationale to prove and record:

1. `Dialogs::MainList` owns `_all` as `IndexedList(SortMode::Date, filterId)`.
2. `Dialogs::InnerWidget` default construction binds `_shownList` to `session.data().chatsList()->indexed()`.
3. Default painting reads `_shownList->all()` in list order.
4. `Dialogs::List` maintains descending existing `Row::sortKey(filterId)` order.
5. The existing entry sort key already carries fixed-on-top, pinned, and date semantics.

Forbidden:

1. Do not iterate `MainList::pinned()` separately.
2. Do not concatenate pinned and unpinned lists.
3. Do not sort by message date, pin index, title, peer ID, or any TG-owned comparator.
4. Do not use a filtered chat list for the default `chats` command.
5. Do not construct `Window::Controller` or a widget.

## Locked Decision 3: History Viewer Error And Timeout Policy

Observed API contract to prove and record:

1. `HistoryMessagesViewer()` is an `rpl::producer<MessagesSlice>` with no error value/channel.
2. Insufficient data causes `HistoryViewer()` to call existing `ApiWrap::requestHistory()`.
3. Success parses the result and calls `history->messages().addSlice(...)`, which produces a sparse-list update and viewer snapshot.
4. A successful empty result has an engaged `std::optional<int>` count of `0`. Static proof must record this full chain: `ApiWrap::requestHistory()` passes `parsed.fullCount` to `HistoryMessages::addSlice()`; `SparseIdsList::addRange()` fires `sliceUpdated`; `SparseIdsSliceBuilder::applyUpdate()` treats engaged count `0` as present (not the same as `std::nullopt`), applies it, and the viewer consumer emits an empty snapshot. Do not label this runtime-observed unless a later executable test actually observes it.
5. Request failure removes `_historyRequests`, calls the histories `finish()` callback, adds no slice, and emits no viewer error/progress.
6. Destroying the subscription lifetime detaches the consumer; it does not need to cancel or mutate Telegram's shared request.

V0 policy:

1. Do not add an error callback seam for A10.2.
2. Each `read`/`more` page creates one viewer subscription and a 30,000 ms first-emission timer.
3. The first `MessagesSlice`, including a valid empty slice, is success.
4. On first emission, stop the timer, copy/format the bounded page, and destroy the subscription lifetime.
5. If no emission occurs by 30,000 ms, destroy the lifetime and return `history-no-progress`.
6. Do not claim whether the cause was network, RPC, or timeout because the existing API does not expose it.
7. Do not automatically retry. A later user command may subscribe again; `requestHistory()` removes failed request bookkeeping.
8. Command cancellation destroys the lifetime and returns `history-cancelled`. Static detach proof must cite `rpl::lifetime::~lifetime()` -> `destroy()` and show that it invokes subscription cleanup callbacks only; it does not remove `_historyRequests`, alter `HistoryMessages`, or cancel Telegram's shared request.

Reopen the additive-error-seam decision only if executable A10.2 work disproves that a successful empty result emits a slice or shows that the timeout cannot detach cleanly. That is a stop condition, not permission to improvise in A10.0.

## Required Evidence Artifact

Create:

`TG_PROBES/NOTE_tg_probe_32_a10_0_api_proofs.md`

The note must contain exactly these sections:

1. `Session And Dialog Readiness`
2. `Desktop Dialog Order`
3. `History Success Empty Failure And Cancellation`
4. `Decisions For A10.1 And A10.2`
5. `Commands And Validation`
6. `Open Risks`

For every conclusion include:

1. source file and symbol;
2. the observed producer/consumer or caller/callee chain;
3. the selected timeout/result token where relevant;
4. whether evidence is static source, existing runtime regression, or executable command;
5. any assumption not directly verified.

Do not paste large source excerpts. Use concise facts and workspace-relative paths.

## Required Roadmap Updates

After evidence is complete:

1. In `PLAN_tg_console_mode.md`:
   - mark A10.0 `[DONE]`;
   - append one outcome line naming selected readiness, list path, and history timeout policy;
   - remove the packet-31 blocking sentence;
   - leave A10.1 as the next item and do not implement it.
2. In `TG_PROBES/PLAN_tg_probe_14_core_extraction.md`:
   - update A10 status to show A10.0 complete and A10.1 next;
   - point to this packet and its note.
3. In this packet:
   - mark completed tasks `[DONE]` one at a time with outcome notes;
   - status becomes ready for explicit user acceptance, not accepted.

## Disposition

1. [DONE] Standalone proof packet retired. **high**
   - Outcome: static conclusions already captured here do not justify a separate implementation gate; A10.1 validates dialog readiness/order and A10.2 validates bounded history behavior against the dev profile.

## Historical Ordered Tasks

1. [TODO] Prove selected-session and top-level dialog readiness chains from the exact listed symbols. **high**
   - Description: verify the immediate/sessionValue branch, explicit dialog request, `chatsListLoaded(nullptr)` immediate/event branch, and why readiness includes pinned dialogs.
   - Definition of Done: evidence note records the exact chain, 15 s session timeout, 30 s dialog timeout, cleanup lifetime, and distinct result tokens; no code changes.

2. [TODO] Prove the exact desktop-order iteration path. **high**
   - Description: trace default widget list selection through `MainList`, `IndexedList`, `List`, and `Row::sortKey` without inspecting unrelated filter/search UI.
   - Definition of Done: evidence note locks `chatsList(nullptr)->indexed()->all()` forward iteration and records why separate pinned concatenation/re-sorting is forbidden.

3. [TODO] Prove history success, empty, failure, timeout, and detach semantics. **high**
   - Description: trace viewer insufficiency through request success/failure and sparse-list emission, including engaged zero count.
   - Definition of Done: evidence note records the complete static empty-success emission chain without claiming runtime observation, proves failure is silent and lifetime destruction only detaches, and locks V0 to one 30 s `history-no-progress` result with no additive seam or retry.

4. [TODO] Run bounded validation and update roadmap records. **high**
   - Description: run checker self-test, fence validation, and `git diff --check`; no full Telegram build is required because A10.0 changes Markdown only.
   - Definition of Done: commands pass; only this packet, its note, the two roadmap Markdown files, and the stale packet-31 status correction change; A10.1 remains unimplemented; packet is ready for user acceptance.

## Validation Commands

Run from repository root with the existing project virtual environment:

```powershell
..\.venv\Scripts\python.exe Telegram\tg_cli\tools\check_tg_change_fences.py --self-test
..\.venv\Scripts\python.exe Telegram\tg_cli\tools\check_tg_change_fences.py --repo . --base 12e8d4a956
git diff --check
git status --short
```

Expected:

1. checker self-test exit `0`;
2. fence validation exit `0`;
3. diff check exit `0`;
4. status contains Markdown evidence/roadmap changes plus the packet-31 status correction only;
5. no source, build, config, manifest, or report file changes.

## Stop Conditions

Stop and record the exact blocker if any occurs:

1. `chatsListLoaded(nullptr)` can fire before pinned and normal dialog readiness.
2. the default desktop list is not `chatsList()->indexed()->all()`.
3. pinned rows require separate concatenation outside the existing list.
4. the complete source chain cannot prove that engaged empty count reaches a viewer emission; if so, record the missing boundary and require a separately approved focused executable probe before A10.2.
5. destroying the viewer lifetime mutates shared history or cannot detach safely.
6. the selected path requires `Window::Controller` construction.
7. proving a decision requires source implementation, a new capability seam, or duplicate Telegram model/request logic.

If stopped, add one `[TODO] CLARIFY` task with source evidence and alternatives; do not start A10.1.

## Definition Of Done

1. All four tasks are `[DONE]` with concise outcome notes.
2. The evidence note exists with all six required sections.
3. Session readiness is locked to A9 account selection/session wait plus explicit dialogs request and `chatsListLoaded(nullptr)`.
4. Dialog order is locked to forward iteration of `chatsList(nullptr)->indexed()->all()`.
5. History policy is locked to first emission success, 30 s `history-no-progress`, cancellation by lifetime destruction, no retry, and no additive error seam for V0.
6. Markdown-only validation passes.
7. No A10.1/A10.2 feature code is written.
8. User acceptance is requested before commit; A10.1 does not begin before this packet is accepted and committed.
