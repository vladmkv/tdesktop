#include "hosted_console_chats_mode.h"

#include <optional>

#include "hosted_console_chats_format.h"
#include "hosted_console_status_writer.h"
#include "settings.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"

#include <QtCore/QEventLoop>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>

#include <rpl/rpl.h>

namespace TgCli::Hosted {
namespace {

[[nodiscard]] bool WriteLine(const QString &line) {
	return WriteHostedConsoleStatusLine(line).ok;
}

[[nodiscard]] bool IsConsoleJsonFormat() {
	return cConsoleFormat().trimmed().toLower() == QStringLiteral("json");
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
	return ok ? std::optional(value) : std::nullopt;
}

[[nodiscard]] Main::Account *SelectedAccount(Main::Domain &domain) {
	auto selectedIndex = domain.activeForStorage();
	const auto requestedIndex = ParseSelectedStorageIndex();
	if (!cConsoleAccountIndex().trimmed().isEmpty() && !requestedIndex) {
		return nullptr;
	}
	if (requestedIndex) {
		selectedIndex = *requestedIndex;
	}
	const auto selected = ranges::find(
		domain.accounts(),
		selectedIndex,
		&Main::Domain::AccountWithIndex::index);
	if (selected == end(domain.accounts())) {
		return nullptr;
	}
	if (selected->account->sessionExists() || requestedIndex) {
		return selected->account.get();
	}
	const auto firstAuthed = ranges::find_if(
		domain.accounts(),
		[](const Main::Domain::AccountWithIndex &entry) {
			return entry.account->sessionExists();
		});
	return (firstAuthed == end(domain.accounts()))
		? nullptr
		: firstAuthed->account.get();
}

[[nodiscard]] Main::Session *WaitForSession(Main::Account &account) {
	if (account.sessionExists()) {
		return &account.session();
	}
	auto session = static_cast<Main::Session*>(nullptr);
	auto loop = QEventLoop();
	auto timer = QTimer();
	auto lifetime = rpl::lifetime();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	account.sessionValue(
	) | rpl::filter([](Main::Session *value) {
		return value != nullptr;
	}) | rpl::take(1) | rpl::on_next([&](Main::Session *value) {
		session = value;
		loop.quit();
	}, lifetime);
	timer.start(15000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	timer.stop();
	lifetime.destroy();
	return session;
}

[[nodiscard]] bool WaitForDialogs(Main::Session &session) {
	if (session.data().chatsListLoaded(nullptr)) {
		return true;
	}
	auto ready = false;
	auto loop = QEventLoop();
	auto timer = QTimer();
	auto lifetime = rpl::lifetime();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	session.data().chatsListLoadedEvents(
	) | rpl::filter([](Data::Folder *folder) {
		return folder == nullptr;
	}) | rpl::take(1) | rpl::on_next([&] {
		ready = true;
		loop.quit();
	}, lifetime);
	session.api().requestDialogs(nullptr);
	timer.start(30000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	timer.stop();
	lifetime.destroy();
	return ready || session.data().chatsListLoaded(nullptr);
}

[[nodiscard]] QString StablePeerId(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return QStringLiteral("user") + QString::number(peerToUser(peer->id).bare);
	}
	if (peer->isChat()) {
		return QStringLiteral("chat") + QString::number(peerToChat(peer->id).bare);
	}
	if (peer->isChannel()) {
		return QStringLiteral("channel") + QString::number(peerToChannel(peer->id).bare);
	}
	return QStringLiteral("unknown");
}

[[nodiscard]] QString PeerType(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return QStringLiteral("user");
	}
	if (peer->isChat()) {
		return QStringLiteral("chat");
	}
	if (peer->isChannel()) {
		return QStringLiteral("channel");
	}
	return QStringLiteral("unknown");
}

[[nodiscard]] QString LastMessageDate(const History &history) {
	const auto last = history.lastMessage();
	return last
		? QDateTime::fromSecsSinceEpoch(last->date(), Qt::UTC).toString(Qt::ISODate)
		: QStringLiteral("none");
}

[[nodiscard]] HostedConsoleChatsOutputData CollectChatsOutputData(
		Main::Session &session) {
	auto output = HostedConsoleChatsOutputData{
		.requestedLimit = cConsoleChatsLimit(),
	};
	const auto &rows = session.data().chatsList(nullptr)->indexed()->all();
	for (auto i = rows.cbegin(), end = rows.cend(); i != end; ++i) {
		if (output.chats.size() == output.requestedLimit) {
			break;
		}
		const auto row = *i;
		const auto history = row->history();
		if (history == nullptr) {
			continue;
		}
		const auto peer = history->peer;
		output.chats.push_back(HostedConsoleChatRow{
			.stableId = StablePeerId(peer),
			.title = peer->name(),
			.type = PeerType(peer),
			.unreadCount = history->unreadCount(),
			.pinned = history->isPinnedDialog(FilterId()),
			.lastMessageDate = LastMessageDate(*history),
		});
	}
	return output;
}

[[nodiscard]] bool WriteChats(const HostedConsoleChatsOutputData &data) {
	if (IsConsoleJsonFormat()
		&& !WriteLine(HostedConsoleChatsJsonLine(data))) {
		return false;
	}
	for (auto index = 0; index != data.chats.size(); ++index) {
		const auto &chat = data.chats[index];
		if (!WriteLine(
				QStringLiteral("chats-row:")
				+ QString::number(index)
				+ QStringLiteral("|")
				+ chat.stableId
				+ QStringLiteral("|")
				+ EscapeTextField(chat.title)
				+ QStringLiteral("|")
				+ chat.type
				+ QStringLiteral("|")
				+ QString::number(chat.unreadCount)
				+ QStringLiteral("|")
				+ QString::number(chat.pinned ? 1 : 0)
				+ QStringLiteral("|")
				+ chat.lastMessageDate)) {
			return false;
		}
	}
	return WriteLine(
		QStringLiteral("chats-count:") + QString::number(data.chats.size()));
}

} // namespace

int RunHostedConsoleChatsMode(Core::Application &application) {
	if (!WriteLine(QStringLiteral("chats-mode:started"))) {
		return 1;
	}
	const auto startResult = application.domain().started()
		? Storage::StartResult::Success
		: application.domain().start(QByteArray());
	if (startResult != Storage::StartResult::Success) {
		if (!WriteLine(QStringLiteral("chats-error:startup"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("chats-mode:done"))) {
			return 1;
		}
		return 1;
	}
	if (!WriteLine(QStringLiteral("chats-start-result:success"))) {
		return 1;
	}
	const auto account = SelectedAccount(application.domain());
	if (account == nullptr) {
		if (!WriteLine(QStringLiteral("chats-error:invalid-account-index"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("chats-mode:done"))) {
			return 1;
		}
		return 1;
	}
	const auto session = WaitForSession(*account);
	if (session == nullptr) {
		if (!WriteLine(QStringLiteral("chats-error:session-timeout"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("chats-mode:done"))) {
			return 1;
		}
		return 1;
	}
	if (!WriteLine(QStringLiteral("chats-session:ready"))) {
		return 1;
	}
	if (!WriteLine(QStringLiteral("chats-dialogs:waiting"))) {
		return 1;
	}
	if (!WaitForDialogs(*session)) {
		if (!WriteLine(QStringLiteral("chats-error:dialogs-timeout"))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("chats-mode:done"))) {
			return 1;
		}
		return 1;
	}
	if (!WriteLine(QStringLiteral("chats-dialogs:ready"))) {
		return 1;
	}
	const auto output = CollectChatsOutputData(*session);
	if (!WriteChats(output)) {
		return 1;
	}
	if (!WriteLine(QStringLiteral("chats-mode:done"))) {
		return 1;
	}
	return 0;
}

} // namespace TgCli::Hosted