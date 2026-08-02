#include "hosted_console_profile_snapshot_diagnostics.h"

#include "main/main_domain.h"
#include "storage/storage_domain.h"

namespace TgCli::Hosted {
namespace {

[[nodiscard]] QString StatusToken(Storage::SnapshotStorageStatus status) {
	switch (status) {
	case Storage::SnapshotStorageStatus::Ready:
		return QStringLiteral("ready");
	case Storage::SnapshotStorageStatus::PasscodeRequired:
		return QStringLiteral("passcode-required");
	case Storage::SnapshotStorageStatus::PasscodeRequiredLegacy:
		return QStringLiteral("passcode-required-legacy");
	case Storage::SnapshotStorageStatus::ProfileCorrupt:
		return QStringLiteral("profile-corrupt");
	case Storage::SnapshotStorageStatus::ProfileNotFound:
		return QStringLiteral("profile-not-found");
	}
	return QStringLiteral("profile-corrupt");
}

} // namespace

QString HostedConsoleProfileSnapshotStatusLine(Main::Domain &domain) {
	const auto status = domain.local().classifySnapshotStorage();
	return QStringLiteral("snapshot-storage-status:") + StatusToken(status);
}

} // namespace TgCli::Hosted
