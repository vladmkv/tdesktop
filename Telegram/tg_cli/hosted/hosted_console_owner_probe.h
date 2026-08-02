#pragma once

#include <QtCore/QString>
#include <QtNetwork/QLocalSocket>

namespace TgCli::Hosted {

enum class HostedConsoleOwnerProbeStatus {
	OwnerBusy = 0,
	OwnerAbsent,
	OwnerAmbiguous,
};

struct HostedConsoleOwnerProbeResult {
	HostedConsoleOwnerProbeStatus status = HostedConsoleOwnerProbeStatus::OwnerAmbiguous;
	QString absoluteWorkdirPath;
	QString localServerName;
	QLocalSocket::LocalSocketState socketState = QLocalSocket::UnconnectedState;
	QLocalSocket::LocalSocketError socketError = QLocalSocket::UnknownSocketError;
	int timeoutMs = 0;
	int elapsedMs = 0;
	QString socketStateToken;
	QString socketErrorToken;
	QString socketErrorString;
	QString detail;
};

[[nodiscard]] HostedConsoleOwnerProbeResult ProbeHostedConsoleOwnerForWorkdir(
	const QString &workdirPath,
	int timeoutMs);

[[nodiscard]] QString HostedConsoleOwnerProbeStatusToken(
	HostedConsoleOwnerProbeStatus status);

} // namespace TgCli::Hosted
