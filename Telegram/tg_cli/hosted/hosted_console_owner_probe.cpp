#include "hosted_console_owner_probe.h"

#include "platform/platform_specific.h"
#include "core/utils.h"

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtNetwork/QLocalSocket>

#include <algorithm>

namespace TgCli::Hosted {
namespace {

[[nodiscard]] QString ComputeLocalServerNameForAbsoluteWorkdir(
		const QString &absoluteWorkdirPath) {
	const auto absolute = QDir(absoluteWorkdirPath).absolutePath();
	const auto encoded = QFile::encodeName(absolute);
	char hash[33] = { 0 };
	hashMd5Hex(encoded.constData(), encoded.size(), hash);
	return Platform::SingleInstanceLocalServerName(QString::fromLatin1(hash));
}

[[nodiscard]] QString SocketStateToken(QLocalSocket::LocalSocketState state) {
	switch (state) {
	case QLocalSocket::UnconnectedState:
		return QStringLiteral("unconnected");
	case QLocalSocket::ConnectingState:
		return QStringLiteral("connecting");
	case QLocalSocket::ConnectedState:
		return QStringLiteral("connected");
	case QLocalSocket::ClosingState:
		return QStringLiteral("closing");
	}
	return QStringLiteral("unknown-state");
}

[[nodiscard]] QString SocketErrorToken(QLocalSocket::LocalSocketError error) {
	switch (error) {
	case QLocalSocket::ConnectionRefusedError:
		return QStringLiteral("connection-refused");
	case QLocalSocket::PeerClosedError:
		return QStringLiteral("peer-closed");
	case QLocalSocket::ServerNotFoundError:
		return QStringLiteral("server-not-found");
	case QLocalSocket::SocketAccessError:
		return QStringLiteral("socket-access");
	case QLocalSocket::SocketResourceError:
		return QStringLiteral("socket-resource");
	case QLocalSocket::SocketTimeoutError:
		return QStringLiteral("socket-timeout");
	case QLocalSocket::DatagramTooLargeError:
		return QStringLiteral("datagram-too-large");
	case QLocalSocket::ConnectionError:
		return QStringLiteral("connection-error");
	case QLocalSocket::UnsupportedSocketOperationError:
		return QStringLiteral("unsupported-operation");
	case QLocalSocket::UnknownSocketError:
		return QStringLiteral("unknown-socket-error");
	case QLocalSocket::OperationError:
		return QStringLiteral("operation-error");
	}
	return QStringLiteral("unknown-socket-error");
}

[[nodiscard]] bool IsDefinitiveOwnerAbsentError(
		QLocalSocket::LocalSocketError error) {
	return (error == QLocalSocket::ServerNotFoundError)
		|| (error == QLocalSocket::ConnectionRefusedError);
}

} // namespace

HostedConsoleOwnerProbeResult ProbeHostedConsoleOwnerForWorkdir(
		const QString &workdirPath,
		int timeoutMs) {
	HostedConsoleOwnerProbeResult result;
	result.absoluteWorkdirPath = QDir(workdirPath).absolutePath();
	result.timeoutMs = std::max(timeoutMs, 0);
	const auto localServerName = ComputeLocalServerNameForAbsoluteWorkdir(
		result.absoluteWorkdirPath);
	result.localServerName = localServerName;

	const auto testMode = qEnvironmentVariable(
		"TG_CLI_OWNER_PROBE_TEST_MODE").trimmed().toLower();
	if (testMode == QStringLiteral("timeout")) {
		result.status = HostedConsoleOwnerProbeStatus::OwnerAmbiguous;
		result.socketState = QLocalSocket::ConnectingState;
		result.socketError = QLocalSocket::SocketTimeoutError;
		result.elapsedMs = result.timeoutMs;
		result.socketStateToken = SocketStateToken(result.socketState);
		result.socketErrorToken = SocketErrorToken(result.socketError);
		result.detail = QStringLiteral("injected-timeout");
		return result;
	}

	QLocalSocket socket;
	QElapsedTimer timer;
	timer.start();
	socket.connectToServer(localServerName);
	(void)socket.waitForConnected(result.timeoutMs);

	result.elapsedMs = int(timer.elapsed());
	result.socketState = socket.state();
	result.socketError = socket.error();
	result.socketErrorString = socket.errorString();
	result.socketStateToken = SocketStateToken(result.socketState);
	result.socketErrorToken = SocketErrorToken(result.socketError);

	if (result.socketState == QLocalSocket::ConnectedState) {
		result.status = HostedConsoleOwnerProbeStatus::OwnerBusy;
		result.detail = QStringLiteral("connected-state");
		socket.disconnectFromServer();
		return result;
	}

	if (IsDefinitiveOwnerAbsentError(result.socketError)) {
		result.status = HostedConsoleOwnerProbeStatus::OwnerAbsent;
		result.detail = result.socketErrorToken;
		return result;
	}

	result.status = HostedConsoleOwnerProbeStatus::OwnerAmbiguous;
	result.detail = QStringLiteral("%1|%2").arg(
		result.socketErrorToken,
		result.socketErrorString);
	return result;
}

QString HostedConsoleOwnerProbeStatusToken(HostedConsoleOwnerProbeStatus status) {
	switch (status) {
	case HostedConsoleOwnerProbeStatus::OwnerBusy:
		return QStringLiteral("owner-busy");
	case HostedConsoleOwnerProbeStatus::OwnerAbsent:
		return QStringLiteral("owner-absent");
	case HostedConsoleOwnerProbeStatus::OwnerAmbiguous:
		return QStringLiteral("owner-ambiguous");
	}
	return QStringLiteral("owner-ambiguous");
}

} // namespace TgCli::Hosted
