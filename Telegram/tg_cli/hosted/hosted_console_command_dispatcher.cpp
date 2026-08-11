#include "hosted_console_command_dispatcher.h"

#include "hosted_console_accounts_mode.h"
#include "hosted_console_chats_mode.h"
#include "hosted_console_read_mode.h"
#include "hosted_console_send_mode.h"
#include "hosted_console_status_writer.h"
#include "settings.h"

namespace TgCli::Hosted {
namespace {

[[nodiscard]] bool WriteLine(const QString &line) {
	return WriteHostedConsoleStatusLine(line).ok;
}

[[nodiscard]] bool IsCommandOption(const QString &value) {
	return value == QStringLiteral("--limit")
		|| value == QStringLiteral("--cursor")
		|| value == QStringLiteral("--text")
		|| value == QStringLiteral("--account-index")
		|| value == QStringLiteral("--format");
}

[[nodiscard]] HostedConsoleCommandParseResult Invalid(const QString &error) {
	return { .error = error };
}

[[nodiscard]] bool ParsePositiveLimit(
		const QString &value,
		int maximum,
		int &result) {
	auto ok = false;
	const auto parsed = value.toInt(&ok);
	if (!ok || parsed < 1 || parsed > maximum
		|| value != QString::number(parsed)) {
		return false;
	}
	result = parsed;
	return true;
}

[[nodiscard]] HostedConsoleCommandRequest LegacyRequest() {
	auto result = HostedConsoleCommandRequest{
		.limit = cConsoleChatsLimit(),
		.chatId = cConsoleReadPeerId(),
		.text = cConsoleSendText(),
		.cursor = cConsoleReadCursor(),
		.accountIndex = cConsoleAccountIndex(),
		.format = cConsoleFormat(),
	};
	if (cConsoleAccountsMode()) {
		result.command = HostedConsoleCommand::Accounts;
	} else if (cConsoleChatsMode()) {
		result.command = HostedConsoleCommand::Chats;
	} else if (cConsoleSendMode()) {
		result.command = HostedConsoleCommand::Send;
	} else {
		result.command = HostedConsoleCommand::Read;
		result.limit = cConsoleReadLimit();
	}
	return result;
}

[[nodiscard]] HostedConsoleCommandParseResult CurrentRequest() {
	if (!cConsoleCommandMode()) {
		return { .request = LegacyRequest() };
	}
	return ParseHostedConsoleCommand(cConsoleCommandArguments());
}

} // namespace

HostedConsoleCommandParseResult ParseHostedConsoleCommand(
		const QStringList &arguments) {
	if (arguments.isEmpty()) {
		return Invalid(QStringLiteral("missing-command"));
	}
	auto result = HostedConsoleCommandRequest{};
	const auto name = arguments.front().trimmed().toLower();
	if (name == QStringLiteral("accounts")) {
		result.command = HostedConsoleCommand::Accounts;
	} else if (name == QStringLiteral("chats")) {
		result.command = HostedConsoleCommand::Chats;
		result.limit = 100;
	} else if (name == QStringLiteral("read")) {
		result.command = HostedConsoleCommand::Read;
		result.limit = 20;
	} else if (name == QStringLiteral("send")) {
		result.command = HostedConsoleCommand::Send;
	} else if (name == QStringLiteral("more")) {
		result.command = HostedConsoleCommand::More;
	} else if (name == QStringLiteral("help")) {
		result.command = HostedConsoleCommand::Help;
	} else if (name == QStringLiteral("quit")) {
		result.command = HostedConsoleCommand::Quit;
	} else {
		return Invalid(QStringLiteral("unknown-command:" ) + name);
	}

	for (auto index = 1; index != arguments.size(); ++index) {
		const auto value = arguments[index];
		if ((result.command == HostedConsoleCommand::Read
			|| result.command == HostedConsoleCommand::Send)
			&& result.chatId.isEmpty()
			&& !IsCommandOption(value)) {
			result.chatId = value;
			continue;
		}
		if (value == QStringLiteral("--text")) {
			if (result.command != HostedConsoleCommand::Send
				|| ++index == arguments.size()) {
				return Invalid(QStringLiteral("invalid-text"));
			}
			result.text = arguments.mid(index).join(QStringLiteral(" "));
			break;
		}
		if (!IsCommandOption(value) || ++index == arguments.size()) {
			return Invalid(QStringLiteral("invalid-arguments"));
		}
		const auto optionValue = arguments[index];
		if (value == QStringLiteral("--limit")) {
			const auto maximum = (result.command == HostedConsoleCommand::Read)
				? 100
				: 1000;
			if ((result.command != HostedConsoleCommand::Chats
				&& result.command != HostedConsoleCommand::Read)
				|| !ParsePositiveLimit(optionValue, maximum, result.limit)) {
				return Invalid(QStringLiteral("invalid-limit"));
			}
		} else if (value == QStringLiteral("--cursor")) {
			if (result.command != HostedConsoleCommand::Read) {
				return Invalid(QStringLiteral("invalid-cursor"));
			}
			result.cursor = optionValue;
		} else if (value == QStringLiteral("--account-index")) {
			if (result.command != HostedConsoleCommand::Accounts
				&& result.command != HostedConsoleCommand::Chats
				&& result.command != HostedConsoleCommand::Read
				&& result.command != HostedConsoleCommand::Send) {
				return Invalid(QStringLiteral("invalid-account-index"));
			}
			result.accountIndex = optionValue;
		} else if (value == QStringLiteral("--format")) {
			if (result.command != HostedConsoleCommand::Accounts
				&& result.command != HostedConsoleCommand::Chats
				&& result.command != HostedConsoleCommand::Read
				&& result.command != HostedConsoleCommand::Send
				|| (optionValue != QStringLiteral("text")
					&& optionValue != QStringLiteral("json"))) {
				return Invalid(QStringLiteral("invalid-format"));
			}
			result.format = optionValue;
		}
	}
	if ((result.command == HostedConsoleCommand::Read
		|| result.command == HostedConsoleCommand::Send)
		&& result.chatId.isEmpty()) {
		return Invalid(QStringLiteral("missing-chat-id"));
	}
	if (result.command == HostedConsoleCommand::Send && result.text.isEmpty()) {
		return Invalid(QStringLiteral("missing-text"));
	}
	return { .request = std::move(result) };
}

bool HostedConsoleCommandRequiresUiInitialization() {
	const auto parsed = CurrentRequest();
	return parsed.request
		&& (parsed.request->command == HostedConsoleCommand::Chats
			|| parsed.request->command == HostedConsoleCommand::Read
			|| parsed.request->command == HostedConsoleCommand::Send);
}

bool ApplyHostedConsoleCommandRequest(const HostedConsoleCommandRequest &request) {
	cSetConsoleAccountsMode(request.command == HostedConsoleCommand::Accounts);
	cSetConsoleChatsMode(request.command == HostedConsoleCommand::Chats);
	cSetConsoleReadMode(request.command == HostedConsoleCommand::Read);
	cSetConsoleSendMode(request.command == HostedConsoleCommand::Send);
	cSetConsoleChatsLimit(request.limit);
	cSetConsoleReadLimit(request.limit);
	cSetConsoleReadPeerId(request.chatId);
	cSetConsoleSendText(request.text);
	cSetConsoleReadCursor(request.cursor);
	cSetConsoleAccountIndex(request.accountIndex);
	cSetConsoleFormat(request.format);
	return request.command == HostedConsoleCommand::Accounts
		|| request.command == HostedConsoleCommand::Chats
		|| request.command == HostedConsoleCommand::Read
		|| request.command == HostedConsoleCommand::Send;
}

HostedConsoleCommandResult RunHostedConsoleCommand(
		Core::Application &application,
		HostedConsoleCommandContext &context) {
	const auto parsed = CurrentRequest();
	if (!parsed.request) {
		const auto written = WriteLine(QStringLiteral("command-error:") + parsed.error);
		(void)written;
		return { .exitCode = 1 };
	}
	return RunHostedConsoleCommand(application, context, *parsed.request);
}

HostedConsoleCommandResult RunHostedConsoleCommand(
		Core::Application &application,
		HostedConsoleCommandContext &context,
		const HostedConsoleCommandRequest &request) {
	if ((request.command == HostedConsoleCommand::Accounts
		|| request.command == HostedConsoleCommand::Chats
		|| request.command == HostedConsoleCommand::Read
		|| request.command == HostedConsoleCommand::Send)
		&& !ApplyHostedConsoleCommandRequest(request)) {
		const auto written = WriteLine(QStringLiteral("command-error:apply-request"));
		(void)written;
		return { .exitCode = 1 };
	}
	switch (request.command) {
	case HostedConsoleCommand::Accounts:
		return { .exitCode = RunHostedConsoleAccountsMode(application) };
	case HostedConsoleCommand::Chats:
		return { .exitCode = RunHostedConsoleChatsMode(application) };
	case HostedConsoleCommand::Read: {
		const auto result = RunHostedConsoleReadMode(application);
		if (result.exitCode == 0) {
			context.previousRead = HostedConsoleReadCursorContext{
				.chatId = request.chatId,
				.cursor = result.nextCursor,
				.limit = request.limit,
			};
		}
		return { .exitCode = result.exitCode };
	}
	case HostedConsoleCommand::Send:
		return { .exitCode = RunHostedConsoleSendMode(application) };
	case HostedConsoleCommand::More: {
		if (!context.previousRead || context.previousRead->cursor.isEmpty()) {
			const auto moreWritten = WriteLine(
				context.previousRead
				? QStringLiteral("command-more:no-successor-read")
				: QStringLiteral("command-more:no-previous-read"));
			(void)moreWritten;
			return {};
		}
		auto next = HostedConsoleCommandRequest{
			.command = HostedConsoleCommand::Read,
			.limit = context.previousRead->limit,
			.chatId = context.previousRead->chatId,
			.cursor = context.previousRead->cursor,
		};
		return RunHostedConsoleCommand(application, context, next);
	}
	case HostedConsoleCommand::Help: {
		const auto helpWritten = WriteLine(QStringLiteral(
			"command-help:accounts|chats [--limit N]|read <chat-id> [--limit N] [--cursor CURSOR]|send <chat-id> --text <text> [--format text|json]|more|help|quit"));
		(void)helpWritten;
		return {};
	}
	case HostedConsoleCommand::Quit: {
		const auto quitWritten = WriteLine(QStringLiteral("command-quit:requested"));
		(void)quitWritten;
		return { .quitRequested = true };
	}
	}
	Unexpected("Hosted console command.");
}

} // namespace TgCli::Hosted