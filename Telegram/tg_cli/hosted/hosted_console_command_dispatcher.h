#pragma once

#include <optional>

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

enum class HostedConsoleCommand {
	Accounts,
	Chats,
	Read,
	Send,
	Edit,
	Delete,
	Confirm,
	Cancel,
	More,
	Help,
	Quit,
};

struct HostedConsoleCommandRequest {
	HostedConsoleCommand command = HostedConsoleCommand::Help;
	int limit = 0;
	int messageId = 0;
	QString chatId;
	QString text;
	QString cursor;
	QString accountIndex;
	QString format;
};

struct HostedConsoleCommandParseResult {
	std::optional<HostedConsoleCommandRequest> request;
	QString error;
};

struct HostedConsoleReadCursorContext {
	QString chatId;
	QString cursor;
	int limit = 0;
};

struct HostedConsoleCommandContext {
	std::optional<HostedConsoleReadCursorContext> previousRead;
	std::optional<HostedConsoleCommandRequest> pendingDelete;
	bool interactive = false;
};

struct HostedConsoleCommandResult {
	int exitCode = 0;
	bool quitRequested = false;
};

[[nodiscard]] HostedConsoleCommandParseResult ParseHostedConsoleCommand(
	const QStringList &arguments);
[[nodiscard]] bool HostedConsoleCommandRequiresUiInitialization();
[[nodiscard]] bool ApplyHostedConsoleCommandRequest(
	const HostedConsoleCommandRequest &request);
[[nodiscard]] HostedConsoleCommandResult RunHostedConsoleCommand(
	Core::Application &application,
	HostedConsoleCommandContext &context);
[[nodiscard]] HostedConsoleCommandResult RunHostedConsoleCommand(
	Core::Application &application,
	HostedConsoleCommandContext &context,
	const HostedConsoleCommandRequest &request);

} // namespace TgCli::Hosted