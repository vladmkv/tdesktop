# Telegram CLI Experiment

This branch explores a terminal-oriented Telegram client while reusing the existing Telegram Desktop runtime. It is an experimental development branch, not an official Telegram Desktop distribution.

The original upstream Telegram Desktop README is preserved in [README.upstream.md](README.upstream.md).

## Current Status

The hosted one-shot CLI works against a dedicated Telegram Desktop development profile. It supports:

- `accounts`
- `chats`
- `read` and paged `more`
- `send`
- `edit`
- `delete`
- experimental JSON output

The interactive terminal REPL is blocked. The hosted executable is a Windows GUI-subsystem process, so a direct PowerShell launch does not reliably retain usable parent-console input/output handles. The pipe-fed REPL smoke passes, but this is not considered a usable human terminal session.

`tg_cli.exe` exists only as a QtCore skeleton. It does not connect to Telegram runtime or provide real commands. The working command implementation is hosted inside the full Telegram Desktop executable, built as `tg.exe`.

## Using The Working Commands

Build the Debug Telegram target, close every Telegram Desktop process using the development profile, then run commands through the wrapper from this repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\Telegram\tg_cli\tools\run_hosted_console_command.ps1 chats -Limit 10
```

Examples:

```powershell
powershell -ExecutionPolicy Bypass -File .\Telegram\tg_cli\tools\run_hosted_console_command.ps1 accounts
powershell -ExecutionPolicy Bypass -File .\Telegram\tg_cli\tools\run_hosted_console_command.ps1 read user123 -Limit 10
powershell -ExecutionPolicy Bypass -File .\Telegram\tg_cli\tools\run_hosted_console_command.ps1 send user123 "hello"
```

Use stable peer selectors from `chats`: `user<ID>`, `chat<ID>`, and `channel<ID>`. Sending, editing, and deleting affect the real account in the selected development profile.

## Architecture And Limits

- The working path is a hosted `tg.exe -console` mode inside Telegram Desktop's linked runtime.
- It reuses existing account, session, chat, history, and API models; it does not implement a second MTProto client or duplicate Telegram data models.
- Development uses a dedicated isolated profile, never a concurrently-used Desktop profile.
- Chats and history do not download media or mark messages read by default.
- Existing upstream-source changes are constrained to named `TG_CHANGE` fences and are checked by `Telegram/tg_cli/tools/check_tg_change_fences.py`.

## Next Decision

Before further REPL work, the frontend/backend architecture must be chosen: a full-runtime console target, a private hosted backend/frontend split, or a separate TDLib client. The planned TDLib/Python alternative is documented in the sibling `tgpy` project.

Detailed scope, evidence, and remaining work are tracked in `PLAN_tg_console_mode.md` and `TG_PROBES/`.
