# PLAN_tg_probe_16_tdlib
Parent: ../PLAN_tg_console_mode.md
Results: NOTE_tg_fallback_architectures.md
Status: [TODO] Decision pending **high**

## Architecture
Use full TDLib as the tg_cli backend with a separate database/profile and independent authentication.

## Locked Tradeoff
- No desktop tdata/session reuse.
- Fresh phone/code/2FA authentication is mandatory.
- Desktop and CLI can run concurrently as separate Telegram sessions.
- Excellent isolation from Telegram Desktop source merges.

## Current Evidence
- Libraries/win64/tde2e contains a full TDLib source checkout.
- Existing preparation configured TD_E2E_ONLY=ON; current built artifacts do not prove Td::TdStatic/TdJson availability.
- A separate full TDLib build/integration probe is required.

## Granular Probes
1. Configure full TDLib in a separate probe build directory without changing prepared tde2e outputs.
2. Build only the required static client interface target on Windows x64 Debug.
3. Link tg_cli_tdlib_probe and initialize a client offline with a temporary database directory.
4. Validate authorization-state transitions without submitting credentials.
5. After explicit approval, authenticate a test account through secure prompts.
6. Probe getChats/getChatHistory and media metadata.
7. Probe send/edit/delete against Saved Messages.
8. Measure Release binary/dependencies and concurrent desktop/CLI behavior.

## Stop Conditions
- Full TDLib cannot build in two iterations with existing dependencies.
- Integration introduces unapproved downloads or incompatible OpenSSL/zlib requirements.
- User rejects separate profile/fresh auth.

## Pass Criteria
- Bounded TDLib target builds and links.
- Required feature APIs work through a thin tg_cli adapter.
- No Qt or Telegram Desktop protected-source dependency.

## Safety
- Never hardcode phone/code/password.
- No authentication or message mutation without explicit user approval.
- Use separate temporary/database paths; never point TDLib at desktop tdata.
