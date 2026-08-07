#include "hosted_console_accounts_format.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace TgCli::Hosted {

QString HostedConsoleAccountsPreflightToken(Storage::SnapshotStorageStatus status) {
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

QString HostedConsoleAccountsStartResultToken(Storage::StartResult result) {
	switch (result) {
	case Storage::StartResult::Success:
		return QStringLiteral("success");
	case Storage::StartResult::IncorrectPasscode:
		return QStringLiteral("incorrect-passcode");
	case Storage::StartResult::IncorrectPasscodeLegacy:
		return QStringLiteral("incorrect-passcode-legacy");
	}
	return QStringLiteral("incorrect-passcode");
}

QString HostedConsoleAccountsJsonLine(const HostedConsoleAccountsOutputData &data) {
	auto object = QJsonObject{
		{ QStringLiteral("preflightStatus"), HostedConsoleAccountsPreflightToken(data.preflightStatus) },
		{ QStringLiteral("startResult"), HostedConsoleAccountsStartResultToken(data.startResult) },
		{ QStringLiteral("accountCount"), data.accountCount },
		{ QStringLiteral("authedCount"), data.authedCount },
		{ QStringLiteral("activeStorageIndex"), data.activeStorageIndex },
		{ QStringLiteral("selectedStorageIndex"), data.selectedStorageIndex },
	};

	auto accounts = QJsonArray();
	for (const auto &account : data.accounts) {
		accounts.push_back(QJsonObject{
			{ QStringLiteral("storageIndex"), account.storageIndex },
			{ QStringLiteral("sessionExists"), account.sessionExists },
			{ QStringLiteral("userId"), QString::number(account.userId) },
			{ QStringLiteral("uniqueId"), QString::number(account.uniqueId) },
			{ QStringLiteral("environment"), account.isTestEnvironment ? QStringLiteral("test") : QStringLiteral("prod") },
			{ QStringLiteral("username"), account.username },
			{ QStringLiteral("displayName"), account.displayName },
		});
	}
	object.insert(QStringLiteral("accounts"), accounts);

	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace TgCli::Hosted
