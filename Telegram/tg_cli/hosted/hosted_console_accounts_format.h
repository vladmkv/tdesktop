#pragma once

#include <QtCore/QString>
#include <QtCore/QStringView>
#include <QtCore/QVector>

#include "storage/storage_domain.h"

namespace TgCli::Hosted {

struct HostedConsoleAccountRow {
	int storageIndex = 0;
	bool sessionExists = false;
	uint64 userId = 0;
	uint64 uniqueId = 0;
	bool isTestEnvironment = false;
	QString username;
	QString displayName;
};

struct HostedConsoleAccountsOutputData {
	Storage::SnapshotStorageStatus preflightStatus = Storage::SnapshotStorageStatus::ProfileCorrupt;
	Storage::StartResult startResult = Storage::StartResult::IncorrectPasscode;
	int accountCount = 0;
	int authedCount = 0;
	int activeStorageIndex = -1;
	int selectedStorageIndex = -1;
	QVector<HostedConsoleAccountRow> accounts;
};

[[nodiscard]] QString HostedConsoleAccountsPreflightToken(Storage::SnapshotStorageStatus status);
[[nodiscard]] QString HostedConsoleAccountsStartResultToken(Storage::StartResult result);
[[nodiscard]] QString HostedConsoleAccountsJsonLine(const HostedConsoleAccountsOutputData &data);

} // namespace TgCli::Hosted
