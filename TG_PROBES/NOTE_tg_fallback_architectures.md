# NOTE_tg_fallback_architectures
Parent: ../PLAN_tg_console_mode.md

## Trigger
Executable Probe 11 failed the selected-existing-source/no-protected-edit architecture after three bounded expansions and 153 unresolved externals. Probes 12/13 are blocked.

## Option A: Minimal reusable-core extraction
Plan: PLAN_tg_probe_14_core_extraction.md
Verdict: CONDITIONAL

Strengths:
- Preserves desktop tdata bootstrap and the mature Main/Data/Api model.
- Avoids reimplementing Telegram update and entity state.

Costs:
- Requires protected edits in active Main/Storage/Core files.
- Ongoing upstream merge conflicts and interface creep are likely.
- Main::Domain has broad UI/application coupling; one host interface was rejected.

Locked design:
- Four narrow capability interfaces: domain lifecycle, account network/config, session services, and storage settings/theme.
- Existing desktop APIs stay unchanged through additive overloads.
- Every protected edit uses named language-valid TG_CHANGE fences.
- An automated checker runs before commits and after upstream merges.
- No fixed line budget; semantic stop conditions reject broad/catch-all interfaces or desktop behavior changes.

Best when:
- Shared desktop profile is mandatory and maintenance accepts recurring rebase work.

## Option B: tg_cli-owned model over low-level MTProto
Plan: PLAN_tg_probe_15_low_level_mtproto.md
Verdict: CONDITIONAL

Strengths:
- No protected edits and clean tg_cli ownership.
- Maximum Telegram Desktop mergeability.

Costs:
- Desktop tdata import requires duplicated proprietary storage parsing.
- Account/entity/update state becomes new tg_cli code.
- Correct update synchronization is the dominant complexity risk.

Best when:
- No protected edits is mandatory and several thousand lines of protocol/model code are acceptable.

## Option C: TDLib backend
Plan: PLAN_tg_probe_16_tdlib.md
Verdict: CONDITIONAL, technically strongest isolation

Strengths:
- Purpose-built headless API for chats/history/send/edit/delete/updates.
- No Qt or Telegram Desktop protected-source coupling.
- Best long-term upstream merge isolation.

Costs:
- Cannot reuse desktop tdata.
- Requires fresh authentication and separate database/profile.
- Existing tde2e preparation built E2E-only; full TDLib build remains to prove.

Best when:
- Separate CLI session/profile is acceptable.

## Option D: Broad desktop source/UI closure
Verdict: NOT RECOMMENDED

Reason:
- Contradicts mergeability and stripping goals.
- Probe 11 already shows closure expands broadly before session construction.
- Produces a second desktop-shaped executable rather than a bounded CLI backend.

## Recommendation
1. If desktop tdata reuse is non-negotiable: choose Option A, accepting protected edits and merge cost.
2. If no protected edits is non-negotiable: choose Option C unless separate auth/profile is unacceptable.
3. Choose Option B only if both protected edits and separate auth are unacceptable; it has the highest implementation complexity.
