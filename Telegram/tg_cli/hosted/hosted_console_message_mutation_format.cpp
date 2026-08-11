#include "hosted_console_message_mutation_format.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace TgCli::Hosted {

QString HostedConsoleMessageMutationJsonLine(
		const HostedConsoleMessageMutationOutputData &data) {
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{ QStringLiteral("operation"), data.operation },
		{ QStringLiteral("chatId"), data.chatId },
		{ QStringLiteral("messageId"), data.messageId },
	}).toJson(QJsonDocument::Compact));
}

} // namespace TgCli::Hosted