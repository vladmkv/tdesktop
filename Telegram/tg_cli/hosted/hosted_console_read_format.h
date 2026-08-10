#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

namespace TgCli::Hosted {

struct HostedConsoleReadRow {
	int messageId = 0;
	QString date;
	QString text;
	QString mediaType;
	QString mediaName;
	qint64 mediaSize = 0;
};

struct HostedConsoleReadOutputData {
	QString chatId;
	int requestedLimit = 0;
	QVector<HostedConsoleReadRow> messages;
	QString nextCursor;
};

[[nodiscard]] QString HostedConsoleReadJsonLine(
	const HostedConsoleReadOutputData &data);

} // namespace TgCli::Hosted