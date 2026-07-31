#pragma once

#include <QtCore/QString>

namespace TgCli::Hosted {

enum class HostedConsoleCheckpointGuardFailure {
	None = 0,
	MissingExplicitWorkdir,
	InvalidWorkdir,
	CanonicalizeFailed,
	ExistingProfileWithoutMarker,
	ExistingProfileNotDisposable,
	MarkerInvalid,
	LockFailed,
	MkdirFailed,
	OpenFailed,
	WriteFailed,
	CommitFailed,
};

struct HostedConsoleCheckpointGuardResult {
	bool ok = false;
	HostedConsoleCheckpointGuardFailure failure = HostedConsoleCheckpointGuardFailure::None;
	bool ownsWorkdirLock = false;
	QString canonicalWorkdirPath;
	QString detail;
};

[[nodiscard]] HostedConsoleCheckpointGuardResult EnforceHostedConsoleCheckpointGuard(
	bool hasExplicitWorkdir,
	const QString &workdirPath);

[[nodiscard]] QString HostedConsoleCheckpointMarkerPath(const QString &workdirPath);
[[nodiscard]] bool HostedConsoleCheckpointOwnsWorkdirLock();

} // namespace TgCli::Hosted
