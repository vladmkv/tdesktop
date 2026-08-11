#pragma once

#include <QtCore/QString>

namespace TgCli::Hosted {

struct HostedConsoleMessageMutationOutputData {
	QString operation;
	QString chatId;
	int messageId = 0;
};

[[nodiscard]] QString HostedConsoleMessageMutationJsonLine(
	const HostedConsoleMessageMutationOutputData &data);

} // namespace TgCli::Hosted