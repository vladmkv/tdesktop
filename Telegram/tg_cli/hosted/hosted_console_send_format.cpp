#include "hosted_console_send_format.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace TgCli::Hosted {

QString HostedConsoleSendJsonLine(const HostedConsoleSendOutputData &data) {
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{ QStringLiteral("chatId"), data.chatId },
	}).toJson(QJsonDocument::Compact));
}

} // namespace TgCli::Hosted