#include "hosted_console_accounts_mode.h"

#include <optional>

#include "hosted_console_accounts_format.h"
#include "hosted_console_status_writer.h"
#include "settings.h"
#include "core/application.h"
#include "data/data_user.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>

#include <rpl/rpl.h>

namespace TgCli::Hosted {
namespace {

[[nodiscard]] bool WriteLine(const QString &line) {
	return WriteHostedConsoleStatusLine(line).ok;
}

[[nodiscard]] bool IsConsoleJsonFormat() {
	const auto format = cConsoleFormat().trimmed().toLower();
	return !format.isEmpty() && (format == QStringLiteral("json"));
}

[[nodiscard]] QString EscapeTextField(QString value) {
	return value
		.replace(u'\\', QStringLiteral("\\\\"))
		.replace(u'|', QStringLiteral("\\|"))
		.replace(u'\r', QStringLiteral("\\r"))
		.replace(u'\n', QStringLiteral("\\n"));
}

[[nodiscard]] std::optional<int> ParseSelectedStorageIndex() {
	const auto text = cConsoleAccountIndex().trimmed();
	if (text.isEmpty()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto value = text.toInt(&ok);
	if (!ok) {
		return std::nullopt;
	}
	return value;
}

[[nodiscard]] bool WaitForAnySession(Main::Domain &domain) {
	if (domain.accountsAuthedCount() > 0) {
		return true;
	}
	auto ready = false;
	auto loop = QEventLoop();
	auto timer = QTimer();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	auto lifetime = rpl::lifetime();
	for (const auto &entry : domain.accounts()) {
		entry.account->sessionValue(
		) | rpl::filter([](Main::Session *session) {
			return session != nullptr;
		}) | rpl::take(1) | rpl::on_next([&] {
			ready = true;
			loop.quit();
		}, lifetime);
	}
	timer.start(15000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	return ready;
}

[[nodiscard]] HostedConsoleAccountsOutputData BuildAccountsOutputData(
	Main::Domain &domain,
	Storage::SnapshotStorageStatus preflightStatus,
	Storage::StartResult startResult,
	int selectedStorageIndex) {
	auto data = HostedConsoleAccountsOutputData{
		.preflightStatus = preflightStatus,
		.startResult = startResult,
		.accountCount = static_cast<int>(domain.accounts().size()),
		.authedCount = domain.accountsAuthedCount(),
		.activeStorageIndex = domain.activeForStorage(),
		.selectedStorageIndex = selectedStorageIndex,
	};

	data.accounts.reserve(data.accountCount);
	for (const auto &[index, account] : domain.accounts()) {
		auto row = HostedConsoleAccountRow{
			.storageIndex = index,
			.sessionExists = account->sessionExists(),
		};
		if (row.sessionExists) {
			const auto &session = account->session();
			row.userId = static_cast<uint64>(session.userId().bare);
			row.uniqueId = session.uniqueId();
			row.isTestEnvironment = session.isTestMode();
			const auto user = session.user();
			row.username = user->username();
			row.displayName = user->name();
		}
		data.accounts.push_back(std::move(row));
	}

	return data;
}

[[nodiscard]] bool WriteAccountsLines(const HostedConsoleAccountsOutputData &data) {
	if (!WriteLine(QStringLiteral("accounts-count:") + QString::number(data.accountCount))) {
		return false;
	}
	if (!WriteLine(QStringLiteral("accounts-authed-count:") + QString::number(data.authedCount))) {
		return false;
	}
	if (!WriteLine(
			QStringLiteral("accounts-active-storage-index:")
			+ QString::number(data.activeStorageIndex))) {
		return false;
	}
	if (!WriteLine(
			QStringLiteral("accounts-selected-storage-index:")
			+ QString::number(data.selectedStorageIndex))) {
		return false;
	}
	for (const auto &account : data.accounts) {
		if (!WriteLine(
				QStringLiteral("accounts-row:")
				+ QString::number(account.storageIndex)
				+ QStringLiteral("|")
				+ QString::number(account.sessionExists ? 1 : 0)
				+ QStringLiteral("|")
				+ QString::number(account.userId)
				+ QStringLiteral("|")
				+ QString::number(account.uniqueId)
				+ QStringLiteral("|")
				+ (account.isTestEnvironment
					? QStringLiteral("test")
					: QStringLiteral("prod"))
				+ QStringLiteral("|")
				+ EscapeTextField(account.username)
				+ QStringLiteral("|")
				+ EscapeTextField(account.displayName))) {
			return false;
		}
	}
	return true;
}

} // namespace

int RunHostedConsoleAccountsMode(Core::Application &application) {
	if (!WriteLine(QStringLiteral("accounts-mode:started"))) {
		return 1;
	}

	const auto profileStatus = application.domain().local().classifySnapshotStorage();
	if (!WriteLine(
			QStringLiteral("accounts-preflight-status:")
			+ HostedConsoleAccountsPreflightToken(profileStatus))) {
		return 1;
	}
	if (profileStatus != Storage::SnapshotStorageStatus::Ready) {
		auto startResult = Storage::StartResult::IncorrectPasscode;
		if (profileStatus == Storage::SnapshotStorageStatus::PasscodeRequiredLegacy) {
			startResult = Storage::StartResult::IncorrectPasscodeLegacy;
		}
		if (!WriteLine(
				QStringLiteral("accounts-start-result:")
				+ HostedConsoleAccountsStartResultToken(startResult))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-count:0"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-authed-count:0"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
			return 1;
		}
		return 1;
	}

	const auto startResult = application.domain().started()
		? Storage::StartResult::Success
		: application.domain().start(QByteArray());
	if (!WriteLine(
			QStringLiteral("accounts-start-result:")
			+ HostedConsoleAccountsStartResultToken(startResult))) {
		return 1;
	}
	if (startResult != Storage::StartResult::Success) {
		if (!WriteLine(QStringLiteral("accounts-count:0"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-authed-count:0"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
			return 1;
		}
		return 1;
	}
	if (!WaitForAnySession(application.domain())) {
		if (!WriteLine(QStringLiteral("accounts-error:timeout"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
			return 1;
		}
		return 1;
	}

	auto selectedStorageIndex = application.domain().activeForStorage();
	const auto requestedSelectedIndex = ParseSelectedStorageIndex();
	if (!cConsoleAccountIndex().trimmed().isEmpty() && !requestedSelectedIndex.has_value()) {
		if (!WriteLine(QStringLiteral("accounts-error:invalid-account-index"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
			return 1;
		}
		return 1;
	}
	if (requestedSelectedIndex.has_value()) {
		selectedStorageIndex = *requestedSelectedIndex;
	} else {
		const auto active = ranges::find(
			application.domain().accounts(),
			selectedStorageIndex,
			&Main::Domain::AccountWithIndex::index);
		if (active == end(application.domain().accounts())
			|| !active->account->sessionExists()) {
			const auto firstAuthed = ranges::find_if(
				application.domain().accounts(),
				[](const Main::Domain::AccountWithIndex &entry) {
					return entry.account->sessionExists();
				});
			if (firstAuthed != end(application.domain().accounts())) {
				selectedStorageIndex = firstAuthed->index;
			}
		}
	}

	const auto output = BuildAccountsOutputData(
		application.domain(),
		profileStatus,
		startResult,
		selectedStorageIndex);

	auto validSelected = false;
	for (const auto &account : output.accounts) {
		if (account.storageIndex == selectedStorageIndex) {
			validSelected = true;
			break;
		}
	}
	if (!validSelected) {
		if (!WriteLine(QStringLiteral("accounts-error:invalid-account-index"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
			return 1;
		}
		return 1;
	}

	auto selectedAccount = (const HostedConsoleAccountRow*)nullptr;
	for (const auto &account : output.accounts) {
		if (account.storageIndex == selectedStorageIndex) {
			selectedAccount = &account;
			break;
		}
	}
	if (output.authedCount <= 0
		|| (selectedAccount == nullptr)
		|| !selectedAccount->sessionExists) {
		if (!WriteLine(QStringLiteral("accounts-error:unexpected-empty-auth"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
			return 1;
		}
		return 1;
	}

	if (IsConsoleJsonFormat()) {
		if (!WriteLine(HostedConsoleAccountsJsonLine(output))) {
			return 1;
		}
	}

	if (!WriteAccountsLines(output)) {
		return 1;
	}
	if (!WriteLine(QStringLiteral("accounts-mode:done"))) {
		return 1;
	}
	return 0;
}

} // namespace TgCli::Hosted
