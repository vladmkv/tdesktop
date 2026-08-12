# TG CLI Session Index And Brief

Session: `fe56307f-e9be-4fd9-9360-d3f7439310b5`  
Exported: 2026-08-12  
Scope: Telegram Desktop CLI investigation, implementation, validation, architecture decision, and documentation.

## Export Files

- Condensed engineering brief: [index.md](index.md)
- Generated per-request audit digest: [digest.md](digest.md)
- Full source-of-truth JSON and Markdown transcripts: [transcript-full.zip](transcript-full.zip)
- Archive identity and checksum: [manifest.md](manifest.md)

`transcript-full.zip` contains the source-of-truth JSON and Markdown reconstructions from VS Code chat storage. The generated digest is useful for request-level provenance, but it is intentionally not the primary project brief because it is large and includes operational detail from the full session.

## Outcome

The original goal was a maintainable terminal Telegram client alongside Telegram Desktop. A standalone `tg_cli.exe` using selected Desktop runtime sources was investigated first, but bounded source-closure probes failed because the required Desktop runtime closure was too broad to maintain safely.

The implemented fallback is a hosted `tg.exe -console` mode inside the full Telegram Desktop runtime. It reuses existing account, session, dialog, history, and API models instead of reimplementing MTProto or Telegram data models.

## What Works

Against a dedicated isolated development profile, the hosted one-shot command path has been built and live-validated for:

- account enumeration
- chat listing with stable IDs
- bounded paged history reads and `more`
- text send
- editing own text where permitted
- confirmed deletion where permitted
- experimental JSON output

Use `Telegram/tg_cli/tools/run_hosted_console_command.ps1` to run these commands. It provides UTF-8 handling, dev-profile defaults, log management, and the actual command exit code.

## Current Blocker

The pipe-fed `-console-repl` loop works, but it is not a usable human terminal REPL. On Windows, `tg.exe` is a GUI-subsystem executable and a direct PowerShell launch does not reliably retain parent-console standard input/output handles. The interactive prompt exits on EOF instead of accepting typed commands.

This blocks A10.3b and the first runnable interactive version. It must not be described as complete merely because redirected-stdin smoke tests pass.

## Architecture Decisions

- Standalone selected-source reuse: closed after bounded dependency-closure failure.
- Current working backend: hosted Telegram Desktop runtime in `tg.exe`.
- `tg_cli.exe`: remains a QtCore-only skeleton; it does not run real Telegram commands.
- Development profile: a dedicated second-device profile outside Git; never run it concurrently with Desktop.
- Live Desktop profile sharing: deferred because robust canonical/alias ownership migration was not solved.
- Next frontend/backend decision: full-runtime console target, private hosted backend/frontend split, or a standalone TDLib client.
- `tgpy`: separately planned Python and TDLib alternative; planning is complete but implementation has not started.

## Durable Records

- Hosted CLI plan: `PLAN_tg_console_mode.md`
- Hosted CLI design note: `NOTE_tg_console_mode.md`
- Probe and architecture records: `TG_PROBES/`
- Current hosted CLI overview: `README.md`
- Planned TDLib alternative: sibling `../tgpy/PLAN_tgpy.md`

## Documentation Produced In This Session

- `tgpy/README.md`: planning-only status and intended TDLib scope.
- Root `README.md`: hosted CLI status, usable one-shot commands, constraints, and REPL blocker.
- `README.upstream.md`: preserved original Telegram Desktop README.

## Verification Snapshot

The hosted command work was previously validated with Debug build, pipe-fed REPL smoke, and `check_tg_change_fences.py`. The documentation edits in this session passed `git diff --check`.

The remaining engineering work begins with an explicit terminal frontend/backend decision, not another terminal-attachment experiment.