# NOTE_tg_executable_probe_results
Parent plan: ../PLAN_tg_console_mode.md

## Stage 1 Skeleton
Verdict: PASS

Evidence:
- BUILD_TG_CLI is OFF by default and adds Telegram/tg_cli only when enabled.
- Windows x64 Debug tg_cli builds as a CUI executable.
- Skeleton links QtCore only.
- tg_cli.exe --help exits 0.
- Existing Telegram/SourceFiles and lib_* files remain unchanged.

## Probe 11: Selected-source compile/link closure
Verdict: FAIL

Attempts:
1. Selected main_account.cpp + storage_account.cpp with reusable MTP/base/storage/UI targets.
   - Compile failed because selected desktop sources require the established stdafx build contract.
2. Added SourceFiles/stdafx.h precompiled-header contract.
   - Compile advanced to missing webview public include/target.
3. Added desktop-app::lib_webview.
   - Compilation succeeded.
   - Link failed with 153 unresolved externals.

Unresolved categories:
- MTP::Instance and connection implementation
- Core::Application and logging/process globals
- Main::Session/settings
- Data::Session, peers, documents, stickers, recent/top peers
- storage/local serialization
- export settings
- Window::Theme and other UI behavior
- CRL main-update integration

Conclusion:
- The closure expands toward the desktop application before Main::Session/Data::Session are even selected.
- Continuing beyond the three-attempt stop condition would violate the bounded-closure criterion.
- No protected source edits were made.
- No profile, network, or auth operation ran.

Independent validation:
- tg_cli_session_probe reproduces LNK1120 with 153 unresolved externals.
- tg_cli skeleton still builds and --help exits 0.

## Probe 12: Qt runtime class
Status: BLOCKED

Reason:
- There is no bounded selected-source session executable to initialize.
- QCoreApplication successfully hosts the skeleton only; this does not answer the session-runtime question.

Next condition:
- Reopen after a fallback backend architecture provides a bounded executable model.

## Probe 13: Minimal account/session construction
Status: BLOCKED

Reason:
- Selected-source closure failed before a constructible account/session graph existed.

Next condition:
- Reopen only after choosing a fallback backend architecture.

## Gate B
Status: FAIL for the chosen "selected desktop sources, no protected edits" architecture.

Required decision:
Choose a fallback before further session/profile work:
1. Permit a small reusable-core extraction/refactor in existing sources.
2. Build a tg_cli-owned high-level model over low-level td_mtproto/td_scheme/storage primitives.
3. Use TDLib with a separate profile and fresh authentication.
4. Accept broad desktop source/UI closure (not recommended; contradicts mergeability goal).
