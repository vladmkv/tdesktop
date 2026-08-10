#include "hosted_console_chats_format.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace TgCli::Hosted {

QString HostedConsoleChatsJsonLine(const HostedConsoleChatsOutputData &data) {
	auto object = QJsonObject{
		{ QStringLiteral("requestedLimit"), data.requestedLimit },
		{ QStringLiteral("count"), data.chats.size() },
	};

	auto chats = QJsonArray();
	for (const auto &chat : data.chats) {
		chats.push_back(QJsonObject{
			{ QStringLiteral("id"), chat.stableId },
			{ QStringLiteral("title"), chat.title },
			{ QStringLiteral("type"), chat.type },
			{ QStringLiteral("unreadCount"), chat.unreadCount },
			{ QStringLiteral("pinned"), chat.pinned },
			{ QStringLiteral("lastMessageDate"), chat.lastMessageDate },
		});
	}
	object.insert(QStringLiteral("chats"), chats);

	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace TgCli::Hosted