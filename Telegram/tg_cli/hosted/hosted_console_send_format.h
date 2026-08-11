#pragma once

#include <QtCore/QString>

namespace TgCli::Hosted {

struct HostedConsoleSendOutputData {
	QString chatId;
};

[[nodiscard]] QString HostedConsoleSendJsonLine(
	const HostedConsoleSendOutputData &data);

} // namespace TgCli::Hosted