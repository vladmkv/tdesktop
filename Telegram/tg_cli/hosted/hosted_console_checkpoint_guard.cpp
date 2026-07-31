#include "hosted_console_checkpoint_guard.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStringList>

#include <utility>

namespace TgCli::Hosted {
namespace {

[[nodiscard]] QString MarkerFileName() {
	return QStringLiteral("tg_hosted_checkpoint.owner");
}

[[nodiscard]] QByteArray MarkerPayload() {
	return QByteArrayLiteral("tg-hosted-checkpoint-v1\n");
}

[[nodiscard]] HostedConsoleCheckpointGuardResult Fail(
	HostedConsoleCheckpointGuardFailure failure,
	QString detail) {
	return {
		.ok = false,
		.failure = failure,
		.detail = std::move(detail),
	};
}

[[nodiscard]] HostedConsoleCheckpointGuardResult EnsureMarker(const QString &workdirPath) {
	const auto tdataPath = workdirPath + QStringLiteral("tdata");
	if (!QDir().mkpath(tdataPath)) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MkdirFailed,
			tdataPath);
	}
	const auto markerPath = HostedConsoleCheckpointMarkerPath(workdirPath);
	QFile marker(markerPath);
	if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::OpenFailed,
			markerPath);
	}
	const auto payload = MarkerPayload();
	const auto written = marker.write(payload.constData(), payload.size());
	if (written != payload.size()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::WriteFailed,
			markerPath);
	}
	if (!marker.flush()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::FlushFailed,
			markerPath);
	}
	return {
		.ok = true,
		.failure = HostedConsoleCheckpointGuardFailure::None,
	};
}

[[nodiscard]] HostedConsoleCheckpointGuardResult ValidateMarker(const QString &workdirPath) {
	const auto markerPath = HostedConsoleCheckpointMarkerPath(workdirPath);
	QFile marker(markerPath);
	if (!marker.open(QIODevice::ReadOnly)) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MarkerInvalid,
			markerPath);
	}
	if (marker.readAll() != MarkerPayload()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MarkerInvalid,
			markerPath);
	}
	return {
		.ok = true,
		.failure = HostedConsoleCheckpointGuardFailure::None,
	};
}

} // namespace

QString HostedConsoleCheckpointMarkerPath(const QString &workdirPath) {
	return workdirPath
		+ QStringLiteral("tdata/")
		+ MarkerFileName();
}

HostedConsoleCheckpointGuardResult EnforceHostedConsoleCheckpointGuard(
	bool hasExplicitWorkdir,
	const QString &workdirPath) {
	if (!hasExplicitWorkdir) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MissingExplicitWorkdir,
			QStringLiteral("console-requires-explicit-workdir"));
	}
	if (workdirPath.isEmpty()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::InvalidWorkdir,
			QStringLiteral("empty-workdir"));
	}
	const auto workdirInfo = QFileInfo(workdirPath);
	if (workdirInfo.exists() && !workdirInfo.isDir()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::InvalidWorkdir,
			workdirPath);
	}
	if (!workdirInfo.exists() && !QDir().mkpath(workdirPath)) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MkdirFailed,
			workdirPath);
	}

	const auto tdataPath = workdirPath + QStringLiteral("tdata");
	const auto tdataInfo = QFileInfo(tdataPath);
	if (!tdataInfo.exists()) {
		return EnsureMarker(workdirPath);
	}
	if (!tdataInfo.isDir()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::InvalidWorkdir,
			tdataPath);
	}

	const auto entries = QDir(tdataPath).entryList(
		QDir::NoDotAndDotDot
			| QDir::AllEntries
			| QDir::Hidden
			| QDir::System);
	if (entries.isEmpty()) {
		return EnsureMarker(workdirPath);
	}
	if (!entries.contains(MarkerFileName())) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::ExistingProfileWithoutMarker,
			tdataPath);
	}
	return ValidateMarker(workdirPath);
}

} // namespace TgCli::Hosted
