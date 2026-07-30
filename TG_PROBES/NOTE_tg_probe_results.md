# NOTE_tg_probe_results
Parent plan: ../PLAN_tg_console_mode.md
Probe plans: PLAN_tg_probe_01_source_closure.md through PLAN_tg_probe_10_dependency_floor.md

## Review Rule
These are static-analysis findings. PASS means the relevant code boundary exists; it does not replace the Stage 2 executable feasibility spike. Claims from delegated reviews were reduced to CONDITIONAL where construction of Main::Session or runtime behavior remains unproven.

## Probe 01: Main/Data/History source closure
Verdict: CONDITIONAL

Findings:
- Main::Domain, Main::Account, Main::Session, Data::Session, History, ApiWrap, and desktop Storage::* implementations are compiled directly into the Telegram target, not exposed as one reusable headless library.
- Lower-level td_mtproto, td_scheme, lib_base, lib_storage, lib_tl, and lib_crl targets are reusable.
- Main::Session constructs a broad subsystem graph, including UI-adjacent features. Individual chat/send APIs may be non-UI, but constructing the object graph remains the central risk.
- The delegated estimate of 15-25 selected sources is not accepted as proven; CMake compile/link closure must measure it.

Decision:
- Stage 2 must compile selected existing sources without modifying them and record the actual closure.
- Pass only if the closure is bounded and maintainable.

Key evidence:
- Telegram/CMakeLists.txt (Main/Data/History sources)
- Telegram/SourceFiles/main/main_session.cpp
- Telegram/SourceFiles/main/main_account.cpp
- Telegram/SourceFiles/data/data_session.cpp

## Probe 02: Qt application runtime
Verdict: CONDITIONAL

Findings:
- Existing Core::Sandbox/Core::Application startup requires QApplication and creates desktop UI before domain startup.
- tg_cli cannot reuse that startup path unchanged.
- A new tg_cli-owned entry point is required.
- Static analysis suggests account/session/network primitives may operate with a Qt event loop, but QCoreApplication sufficiency is not proven because selected desktop sources retain GUI/UI dependencies.

Decision:
- Stage 1 uses QCoreApplication only for the empty skeleton.
- Stage 2 tests QCoreApplication first, then QApplication without creating windows if required.
- QApplication is an acceptable initial floor; removing it is not a Stage 2 requirement.

Key evidence:
- Telegram/SourceFiles/core/sandbox.h
- Telegram/SourceFiles/core/application.cpp
- Telegram/SourceFiles/main/main_session.cpp

## Probe 03: Shared profile ownership
Verdict: CONDITIONAL

Findings:
- Telegram's effective workdir is selected from portable mode, explicit -workdir, or platform default behavior.
- Single-instance ownership uses a QLocalServer name derived from the normalized working-directory path and a fixed GUID.
- The executable-path QLockFile is not sufficient profile ownership.
- Storage uses atomic QSaveFile replacement but no independent storage-wide interprocess transaction lock was found.
- A check-then-open sequence races; tg_cli must acquire and retain the same QLocalServer ownership before reading tdata.
- Different path aliases/junctions/casing can hash differently and bypass mutual exclusion.

Decision:
- Stage 3 uses fail-closed QLocalServer acquisition compatible with tg.
- Any ambiguous IPC error is PROFILE_BUSY.
- The server remains owned until storage teardown/sync finishes.
- Custom workdirs must be canonicalized and alias-prone paths rejected/documented.
- No profile is opened until ownership is acquired.

Key evidence:
- Telegram/SourceFiles/core/launcher.cpp
- Telegram/SourceFiles/core/sandbox.cpp
- Telegram/SourceFiles/platform/win/specific_win.cpp
- Telegram/SourceFiles/storage/details/storage_file_utilities.cpp

## Probe 04: Local profile passcode
Verdict: CONDITIONAL

Findings:
- Local profile passcode unlock is separate from Telegram cloud 2FA.
- Main::Domain::start(QByteArray) delegates to Storage::Domain::start(); passcode bytes are UTF-8.
- Modern and legacy wrong-passcode outcomes are distinct.
- No UI controller is required for the storage-level passcode API.
- Storage itself does not enforce desktop retry throttling.
- Corrupt/missing key paths can fall into start-from-scratch behavior, which is unsafe for a diagnostic CLI pointed at the wrong profile.

Decision:
- Secure Windows no-echo prompt, console-mode restoration, practical buffer clearing, and desktop-equivalent retry delays are required.
- Stage 3 remains read-only until a valid existing account is confirmed.
- Wrong passcode, corrupt profile, missing profile, and wrong workdir must be distinct and must never initialize replacement storage.

Key evidence:
- Telegram/SourceFiles/main/main_domain.cpp
- Telegram/SourceFiles/storage/storage_domain.cpp
- Telegram/SourceFiles/storage/storage_domain.h
- Telegram/SourceFiles/storage/details/storage_file_utilities.cpp
- Telegram/SourceFiles/window/window_lock_widgets.cpp

## Probe 05: Account enumeration and selection
Verdict: CONDITIONAL PASS

Findings:
- Storage::Domain loads account entries before Main::Session/Data::Session construction.
- Account index and user/session identity are available early enough for a minimal selector.
- Rich display metadata such as username/phone may not be available before starting the selected session.

Decision:
- Initial selector displays account index and stable user/session ID.
- Only the selected account proceeds to full session startup.
- Stage 3 runtime test must verify that non-selected accounts are not activated unintentionally.

Key evidence:
- Telegram/SourceFiles/storage/storage_domain.cpp
- Telegram/SourceFiles/main/main_domain.h
- Telegram/SourceFiles/main/main_account.cpp
- Telegram/SourceFiles/storage/storage_account.cpp

## Probe 06: Chat listing and history paging
Verdict: CONDITIONAL PASS

Findings:
- Data::Session exposes chat-list/history models without requiring Window::SessionController parameters.
- ApiWrap/Data::Histories provide MTProto history requests and paging primitives.
- Server request, model ingestion, and UI presentation are separable in code.
- This path still depends on successfully constructing Main::Session/Data::Session, so it is not independently proven runnable.
- Media object access may trigger preload/download behavior unless explicitly suppressed.

Decision:
- Stage 4 uses existing Data/Api history paths behind tg_cli DTO adapters.
- First implementation must suppress media downloads and flatten TextWithEntities deliberately.
- Explicit watch support requires a separately verified update subscription path.

Key evidence:
- Telegram/SourceFiles/apiwrap.cpp
- Telegram/SourceFiles/data/data_session.h
- Telegram/SourceFiles/data/data_histories.cpp
- Telegram/SourceFiles/data/data_history_messages.h

## Probe 07: Plain-text send
Verdict: CONDITIONAL PASS

Findings:
- Non-UI sending paths exist through ApiWrap/Data::Histories.
- Random IDs provide request idempotency and map optimistic local IDs to server IDs.
- Permission prechecks and server error callbacks exist.
- Some desktop error handlers display UI; tg_cli must map server failures to its own error taxonomy.
- The path requires a functioning Main::Session/Data::Session/History object graph.

Decision:
- Stage 5 uses the lowest practical non-UI send path and tg_cli-owned completion/error mapping.
- Success means server acknowledgment/update mapping, not merely request dispatch.

Key evidence:
- Telegram/SourceFiles/apiwrap.cpp
- Telegram/SourceFiles/data/data_histories.cpp
- Telegram/SourceFiles/api/api_updates.cpp
- Telegram/SourceFiles/data/data_chat_participant_status.h

## Probe 08: Stable CLI chat IDs
Verdict: PASS

Decision:
- Canonical grammar:
  - user<bare-id>
  - chat<bare-id>
  - channel<bare-id>
- bare-id is non-zero decimal and must fit the existing typed ID representation.
- JSON representation is a string.
- Parsing is strict and reversible using peerFromUser/Chat/Channel helpers.
- Usernames are optional resolution aliases and are never persisted as canonical IDs.

Examples:
- user777000
- chat12345678
- channel1234567890

Key evidence:
- Telegram/SourceFiles/data/data_peer_id.h
- Telegram/SourceFiles/data/data_peer_id.cpp
- Telegram/SourceFiles/export/output/export_output_json.cpp

## Probe 09: Edit/delete APIs
Verdict: CONDITIONAL PASS

Findings:
- Api::EditTextMessage and Data::Histories::deleteMessages are non-UI request paths.
- Existing HistoryItem permission checks cover ownership, edit/revoke windows, and channel rights.
- Server updates drive final local state for edits/deletions, reducing inconsistent optimistic mutation risk.
- The operations still require the functioning high-level session/history object graph.

Decision:
- Stage 6 uses existing permission checks plus server-authoritative failures.
- CLI confirmation policy remains independent of Telegram permission policy.
- No local success is reported before server acknowledgment/update processing.

Key evidence:
- Telegram/SourceFiles/api/api_editing.cpp
- Telegram/SourceFiles/history/history_item.cpp
- Telegram/SourceFiles/data/data_histories.cpp
- Telegram/SourceFiles/api/api_updates.cpp

## Probe 10: Qt/UI dependency floor
Verdict: CONDITIONAL

Accepted initial floor:
- QtCore, QtNetwork
- lib_base, lib_crl, lib_storage, lib_tl
- QApplication/QtGui/QtWidgets/lib_ui as required by the measured selected-source closure

Target reduction order after behavior works:
1. Desktop-only dialogs/window source clusters not referenced by CLI paths.
2. Notification and popup/error presentation paths replaced by tg_cli-owned adapters.
3. QtWidgets linkage where no remaining symbol requires it.
4. lib_ui widget/layer modules while retaining required text/media data types.
5. QtGui subset only after runtime measurement.

Corrections to delegated report:
- QCoreApplication versus QApplication is unresolved until Stage 2 runtime probing.
- Windows offscreen assumptions are not accepted as established.
- Stubbing calls inside existing source is not allowed; tg_cli-owned adapters or different source boundaries must achieve removal without protected edits.

Decision:
- Stage 7 removes one cluster per iteration and runs the complete profile/read/send/edit/delete matrix after each.
- The first failing removal defines an accepted dependency floor unless a new architecture decision is approved.

## Static Probe Summary
- PASS: 08
- CONDITIONAL PASS: 05, 06, 07, 09
- CONDITIONAL: 01, 02, 03, 04, 10
- FAIL: none at static-analysis level

## Stage 2 Go/No-Go
Static evidence supports proceeding to executable feasibility probes, but does not yet pass Gate B.

Gate B still requires:
1. Measured selected-source CMake closure.
2. Successful minimal account/session initialization.
3. Proven Qt application runtime choice on Windows x64 Debug.
4. No protected upstream source edits.
