#include "hosted_console_checkpoint_guard.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLockFile>
#include <QtCore/QSaveFile>
#include <QtCore/QStringList>

#include <memory>
#include <utility>

namespace TgCli::Hosted {
namespace {

[[nodiscard]] QString MarkerFileName() {
	return QStringLiteral("tg_hosted_checkpoint.owner");
}

[[nodiscard]] QString ConsoleBootstrapLogFileName() {
	return QStringLiteral("console_bootstrap.log");
}

[[nodiscard]] QString RuntimeLockFileName() {
	return QStringLiteral("tg_hosted_checkpoint.runtime.lock");
}

[[nodiscard]] QString InitLockFileName() {
	return QStringLiteral("tg_hosted_checkpoint.init.lock");
}

[[nodiscard]] int InitLockTimeoutMs() {
	return 3000;
}

[[nodiscard]] QByteArray MarkerPayload() {
	return QByteArrayLiteral("tg-hosted-checkpoint-v1\n");
}

std::unique_ptr<QLockFile> RuntimeLock;

[[nodiscard]] HostedConsoleCheckpointGuardResult Fail(
	HostedConsoleCheckpointGuardFailure failure,
	QString detail) {
	return {
		.ok = false,
		.failure = failure,
		.ownsWorkdirLock = false,
		.detail = std::move(detail),
	};
}

[[nodiscard]] QString EnsureWorkdirTrailingSlash(QString path) {
	if (!path.endsWith('/')) {
		path += '/';
	}
	return path;
}

[[nodiscard]] HostedConsoleCheckpointGuardResult CanonicalizeWorkdir(
	const QString &workdirPath) {
	if (workdirPath.isEmpty()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::InvalidWorkdir,
			QStringLiteral("empty-workdir"));
	}
	const auto absolute = QDir(workdirPath).absolutePath();
	const auto absoluteInfo = QFileInfo(absolute);
	if (absoluteInfo.exists() && !absoluteInfo.isDir()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::InvalidWorkdir,
			absolute);
	}
	if (!absoluteInfo.exists() && !QDir().mkpath(absolute)) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MkdirFailed,
			absolute);
	}
	const auto canonical = QFileInfo(absolute).canonicalFilePath();
	if (canonical.isEmpty()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::CanonicalizeFailed,
			absolute);
	}
	const auto canonicalInfo = QFileInfo(canonical);
	if (!canonicalInfo.exists() || !canonicalInfo.isDir()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::CanonicalizeFailed,
			canonical);
	}
	auto result = HostedConsoleCheckpointGuardResult{
		.ok = true,
		.failure = HostedConsoleCheckpointGuardFailure::None,
		.ownsWorkdirLock = false,
		.canonicalWorkdirPath = EnsureWorkdirTrailingSlash(canonical),
	};
	return result;
}

[[nodiscard]] HostedConsoleCheckpointGuardResult EnsureMarker(const QString &workdirPath) {
	const auto tdataPath = workdirPath + QStringLiteral("tdata");
	if (!QDir().mkpath(tdataPath)) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::MkdirFailed,
			tdataPath);
	}
	const auto markerPath = HostedConsoleCheckpointMarkerPath(workdirPath);
	QSaveFile marker(markerPath);
	if (!marker.open(QIODevice::WriteOnly)) {
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
	if (!marker.commit()) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::CommitFailed,
			markerPath);
	}
	return {
		.ok = true,
		.failure = HostedConsoleCheckpointGuardFailure::None,
		.ownsWorkdirLock = false,
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
		.ownsWorkdirLock = false,
	};
}

[[nodiscard]] HostedConsoleCheckpointGuardResult ValidateDisposableCheckpoint(
	const QString &workdirPath,
	bool allowMarkerCreate) {
	const auto tdataPath = workdirPath + QStringLiteral("tdata");
	const auto tdataInfo = QFileInfo(tdataPath);
	if (!tdataInfo.exists()) {
		return allowMarkerCreate
			? EnsureMarker(workdirPath)
			: Fail(
				HostedConsoleCheckpointGuardFailure::MarkerInvalid,
				HostedConsoleCheckpointMarkerPath(workdirPath));
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
		return allowMarkerCreate
			? EnsureMarker(workdirPath)
			: Fail(
				HostedConsoleCheckpointGuardFailure::MarkerInvalid,
				HostedConsoleCheckpointMarkerPath(workdirPath));
	}
	if (!entries.contains(MarkerFileName())) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::ExistingProfileWithoutMarker,
			tdataPath);
	}
	if (!allowMarkerCreate) {
		return ValidateMarker(workdirPath);
	}
	if (entries.size() > 2) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::ExistingProfileNotDisposable,
			tdataPath);
	}
	for (const auto &entry : entries) {
		if (entry == MarkerFileName() || entry == ConsoleBootstrapLogFileName()) {
			continue;
		}
		return Fail(
			HostedConsoleCheckpointGuardFailure::ExistingProfileNotDisposable,
			tdataPath + QStringLiteral("/") + entry);
	}
	return ValidateMarker(workdirPath);
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

	const auto canonical = CanonicalizeWorkdir(workdirPath);
	if (!canonical.ok) {
		return canonical;
	}
	const auto canonicalWorkdir = canonical.canonicalWorkdirPath;

	const auto initLockPath = canonicalWorkdir + InitLockFileName();
	auto initLock = QLockFile(initLockPath);
	initLock.setStaleLockTime(0);
	if (!initLock.tryLock(InitLockTimeoutMs())) {
		return Fail(
			HostedConsoleCheckpointGuardFailure::LockFailed,
			initLockPath);
	}

	auto runtimeLock = std::make_unique<QLockFile>(
		canonicalWorkdir + RuntimeLockFileName());
	runtimeLock->setStaleLockTime(0);
	const auto ownsWorkdirLock = runtimeLock->tryLock();

	const auto validated = ValidateDisposableCheckpoint(
		canonicalWorkdir,
		ownsWorkdirLock);
	if (!validated.ok) {
		return validated;
	}

	auto result = HostedConsoleCheckpointGuardResult{
		.ok = true,
		.failure = HostedConsoleCheckpointGuardFailure::None,
		.ownsWorkdirLock = ownsWorkdirLock,
		.canonicalWorkdirPath = canonicalWorkdir,
	};
	if (ownsWorkdirLock) {
		RuntimeLock = std::move(runtimeLock);
	}
	return result;
}

bool HostedConsoleCheckpointOwnsWorkdirLock() {
	return RuntimeLock && RuntimeLock->isLocked();
}

} // namespace TgCli::Hosted
