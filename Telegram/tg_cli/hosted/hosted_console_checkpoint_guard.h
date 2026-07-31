#pragma once

#include <QtCore/QString>

namespace TgCli::Hosted {

enum class HostedConsoleCheckpointGuardFailure {
	None = 0,
	MissingExplicitWorkdir,
	InvalidWorkdir,
	ExistingProfileWithoutMarker,
	MarkerInvalid,
	MkdirFailed,
	OpenFailed,
	WriteFailed,
	FlushFailed,
};

struct HostedConsoleCheckpointGuardResult {
	bool ok = false;
	HostedConsoleCheckpointGuardFailure failure = HostedConsoleCheckpointGuardFailure::None;
	QString detail;
};

[[nodiscard]] HostedConsoleCheckpointGuardResult EnforceHostedConsoleCheckpointGuard(
	bool hasExplicitWorkdir,
	const QString &workdirPath);

[[nodiscard]] QString HostedConsoleCheckpointMarkerPath(const QString &workdirPath);

} // namespace TgCli::Hosted
