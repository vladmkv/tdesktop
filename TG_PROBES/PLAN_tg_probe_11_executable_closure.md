# PLAN_tg_probe_11_executable_closure
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_executable_probe_results.md
Status: [DONE] FAIL

## Question
What exact selected-source compile/link closure is required for tg_cli to use Main::Account/Main::Session/Data::Session without modifying existing sources?

## Probe Design
1. Add a probe-only target/source set under Telegram/tg_cli/probes.
2. Start with the smallest Main/Storage source boundary supported by static findings.
3. Build Debug target only and capture compiler/linker failures.
4. Classify each missing dependency as:
- existing reusable CMake target
- additional selected existing source
- tg_cli-owned adapter seam
- protected-source blocker
5. Stop after three non-converging closure expansions and report CONDITIONAL/FAIL instead of absorbing most of Telegram.

## Pass Criteria
- Probe links with a bounded, documented source/target set.
- No protected existing source edits.
- Closure does not approach the full Telegram target.

## Fail Criteria
- Three expansions do not converge.
- Required symbols imply broad desktop UI/window startup closure.
- Existing source behavior requires protected-source edits.

## Validation
- cmake --build .\out --config Debug --target tg_cli_session_probe
- Record target source count, linked targets, and unresolved categories.

## Results Summary
Verdict: FAIL. The selected-source closure did not converge within three attempts. No profile data was opened and no network or authentication path was run.

Attempt 1:
- Sources (3): tg_cli/probes/session_probe_main.cpp, SourceFiles/main/main_account.cpp, SourceFiles/storage/storage_account.cpp.
- Linked targets (8): td_mtproto, td_scheme, desktop-app::lib_base, desktop-app::lib_crl, desktop-app::lib_storage, desktop-app::lib_tl, desktop-app::lib_ui, desktop-app::external_qt.
- Result: compile failure. The selected desktop sources require Telegram's SourceFiles/stdafx.h compile contract; missing generated MTProto types, Qt object definitions, and RPL producers caused cascading compiler diagnostics.
- Category: target build-context requirement, not yet a source-closure measurement.

Attempt 2:
- Sources and linked targets: unchanged from Attempt 1.
- Build-context expansion: use SourceFiles/stdafx.h as the target precompiled header.
- Result: compilation advanced to one missing public include, webview/webview_common.h.
- Category: existing reusable CMake target, desktop-app::lib_webview.

Attempt 3:
- Sources (3): unchanged.
- Linked targets (9): Attempt 2 set plus desktop-app::lib_webview.
- Result: all selected sources compiled; link failed with LNK1120 and 153 unresolved externals. The diagnostic owners included storage_account.obj (at least 100), main_account.obj (at least 30), MTProto objects, and lib_ui objects.
- Additional selected existing-source categories: MTP::Instance/connection implementation, Main::Session and settings, Data::Session and peer/document/sticker/recent-peer models, storage serialization/local-storage, export settings, and Window theme implementation.
- tg_cli-owned adapter categories: process settings/logging globals and the CRL main-update hook. These are insufficient by themselves because the selected sources also call Core::Application and desktop session/UI behavior directly.
- Protected-source blocker category: Storage::Account and Main::Account directly reference Core::Application, Window::Theme, MainWidget/window behavior, media audio, and broad session/data implementations. Satisfying those symbols by selection approaches desktop application/UI closure before Main::Session or Data::Session are added; removing the calls would require prohibited protected-source edits.

Validation output:
- Attempt 1: Debug target failed during compilation with C2146/C2065/C2504 and related cascading type errors.
- Attempt 2: Debug target failed during compilation with C1083 for webview/webview_common.h.
- Attempt 3: Debug target compiled the three selected sources, then failed at link with fatal error LNK1120: 153 unresolved externals.
- Protected SourceFiles and lib_* paths: unchanged.
