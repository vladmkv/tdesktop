#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

namespace TgCli::Hosted {

struct HostedConsoleChatRow {
	QString stableId;
	QString title;
	QString type;
	int unreadCount = 0;
	bool pinned = false;
	QString lastMessageDate;
};

struct HostedConsoleChatsOutputData {
	int requestedLimit = 0;
	QVector<HostedConsoleChatRow> chats;
};

[[nodiscard]] QString HostedConsoleChatsJsonLine(
	const HostedConsoleChatsOutputData &data);

} // namespace TgCli::Hosted