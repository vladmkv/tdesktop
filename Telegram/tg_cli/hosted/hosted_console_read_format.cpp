#include "hosted_console_read_format.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace TgCli::Hosted {

QString HostedConsoleReadJsonLine(const HostedConsoleReadOutputData &data) {
	auto object = QJsonObject{
		{ QStringLiteral("chatId"), data.chatId },
		{ QStringLiteral("requestedLimit"), data.requestedLimit },
		{ QStringLiteral("count"), data.messages.size() },
		{ QStringLiteral("nextCursor"), data.nextCursor },
	};

	auto messages = QJsonArray();
	for (const auto &message : data.messages) {
		messages.push_back(QJsonObject{
			{ QStringLiteral("id"), message.messageId },
			{ QStringLiteral("date"), message.date },
			{ QStringLiteral("text"), message.text },
			{ QStringLiteral("mediaType"), message.mediaType },
			{ QStringLiteral("mediaName"), message.mediaName },
			{ QStringLiteral("mediaSize"), message.mediaSize },
		});
	}
	object.insert(QStringLiteral("messages"), messages);

	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace TgCli::Hosted