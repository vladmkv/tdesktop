#include "hosted_console_read_mode.h"

#include <optional>

#include "hosted_console_read_format.h"
#include "hosted_console_status_writer.h"
#include "settings.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_history_messages.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
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

#include <QtCore/QDateTime>
#include <QtCore/QEventLoop>
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

[[nodiscard]] std::optional<PeerId> ParseStablePeerId(const QString &value) {
	auto digits = value;
	auto type = QString();
	if (digits.startsWith(QStringLiteral("user"))) {
		type = QStringLiteral("user");
	} else if (digits.startsWith(QStringLiteral("chat"))) {
		type = QStringLiteral("chat");
	} else if (digits.startsWith(QStringLiteral("channel"))) {
		type = QStringLiteral("channel");
	} else {
		return std::nullopt;
	}
	digits.remove(0, type.size());
	auto ok = false;
	const auto bare = digits.toLongLong(&ok);
	if (!ok || bare <= 0 || digits != QString::number(bare)) {
		return std::nullopt;
	}
	if (type == QStringLiteral("user")) {
		return peerFromUser(UserId(bare));
	} else if (type == QStringLiteral("chat")) {
		return peerFromChat(ChatId(bare));
	}
	return peerFromChannel(ChannelId(bare));
}

[[nodiscard]] QString ReadCursor(
		const QString &stableId,
		Data::MessagePosition position) {
	return QStringLiteral("v1:")
		+ stableId
		+ QStringLiteral(":")
		+ QString::number(position.fullId.msg.bare)
		+ QStringLiteral(":")
		+ QString::number(position.date);
}

[[nodiscard]] std::optional<Data::MessagePosition> ParseReadCursor(
		const QString &value,
		PeerId peerId,
		const QString &stableId) {
	if (value.isEmpty()) {
		return Data::MaxMessagePosition;
	}
	const auto parts = value.split(u':');
	if (parts.size() != 4
		|| parts[0] != QStringLiteral("v1")
		|| parts[1] != stableId) {
		return std::nullopt;
	}
	auto messageIdOk = false;
	auto dateOk = false;
	const auto messageId = parts[2].toInt(&messageIdOk);
	const auto date = parts[3].toInt(&dateOk);
	if (!messageIdOk
		|| !dateOk
		|| messageId < 1
		|| messageId >= ServerMaxMsgId
		|| date < 1
		|| parts[2] != QString::number(messageId)
		|| parts[3] != QString::number(date)) {
		return std::nullopt;
	}
	return Data::MessagePosition{
		.fullId = FullMsgId(peerId, MsgId(messageId)),
		.date = TimeId(date),
	};
}

[[nodiscard]] History *ResolveLoadedHistory(
		Main::Session &session,
		const QString &stableId) {
	const auto peerId = ParseStablePeerId(stableId);
	if (!peerId || session.data().peerLoaded(*peerId) == nullptr) {
		return nullptr;
	}
	return session.data().history(*peerId).get();
}

[[nodiscard]] QString MediaType(const Data::Media *media) {
	if (media == nullptr) {
		return QStringLiteral("none");
	}
	if (media->document() != nullptr) {
		return QStringLiteral("document");
	}
	if (media->photo() != nullptr) {
		return QStringLiteral("photo");
	}
	return QStringLiteral("other");
}

[[nodiscard]] HostedConsoleReadRow ReadRow(
		Main::Session &session,
		const FullMsgId &id,
		Data::MessagePosition &position) {
	auto result = HostedConsoleReadRow{};
	const auto item = session.data().message(id);
	if (item == nullptr) {
		return result;
	}
	position = item->position();
	result.messageId = item->fullId().msg.bare;
	result.date = QDateTime::fromSecsSinceEpoch(
		item->date(),
		Qt::UTC).toString(Qt::ISODate);
	result.text = item->originalText().text;
	const auto media = item->media();
	result.mediaType = MediaType(media);
	if (const auto document = media ? media->document() : nullptr) {
		result.mediaName = document->filename();
		result.mediaSize = document->size;
	} else if (const auto photo = media ? media->photo() : nullptr) {
		result.mediaName = QString::number(photo->width())
			+ QStringLiteral("x")
			+ QString::number(photo->height());
	}
	return result;
}

[[nodiscard]] std::optional<HostedConsoleReadOutputData> WaitForPage(
		Main::Session &session,
		not_null<History*> history,
		const QString &stableId,
		Data::MessagePosition around) {
	auto output = std::optional<HostedConsoleReadOutputData>();
	auto loop = QEventLoop();
	auto timer = QTimer();
	auto lifetime = rpl::lifetime();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	Data::HistoryMessagesViewer(
		history,
		around,
		cConsoleReadLimit() + ((around == Data::MaxMessagePosition) ? 0 : 1),
		0
	) | rpl::take(1) | rpl::on_next([&](Data::MessagesSlice slice) {
		auto result = HostedConsoleReadOutputData{
			.chatId = stableId,
			.requestedLimit = cConsoleReadLimit(),
		};
		for (const auto &id : slice.ids) {
			if (id == around.fullId) {
				continue;
			}
			if (result.messages.size() == result.requestedLimit) {
				break;
			}
			auto position = Data::MessagePosition{};
			const auto row = ReadRow(session, id, position);
			if (row.messageId != 0) {
				if (result.nextCursor.isEmpty()) {
					result.nextCursor = ReadCursor(stableId, position);
				}
				result.messages.push_back(row);
			}
		}
		output = std::move(result);
		loop.quit();
	}, lifetime);
	timer.start(30000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	timer.stop();
	lifetime.destroy();
	return output;
}

[[nodiscard]] bool WriteRead(const HostedConsoleReadOutputData &data) {
	if (IsConsoleJsonFormat() && !WriteLine(HostedConsoleReadJsonLine(data))) {
		return false;
	}
	for (auto index = 0; index != data.messages.size(); ++index) {
		const auto &message = data.messages[index];
		if (!WriteLine(
				QStringLiteral("read-row:")
				+ QString::number(index)
				+ QStringLiteral("|")
				+ QString::number(message.messageId)
				+ QStringLiteral("|")
				+ message.date
				+ QStringLiteral("|")
				+ EscapeTextField(message.text)
				+ QStringLiteral("|")
				+ message.mediaType
				+ QStringLiteral("|")
				+ EscapeTextField(message.mediaName)
				+ QStringLiteral("|")
				+ QString::number(message.mediaSize))) {
			return false;
		}
	}
	return WriteLine(
		QStringLiteral("read-count:") + QString::number(data.messages.size()))
		&& WriteLine(QStringLiteral("read-next-cursor:") + data.nextCursor);
}

} // namespace

int RunHostedConsoleReadMode(Core::Application &application) {
	if (!WriteLine(QStringLiteral("read-mode:started"))) {
		return 1;
	}
	const auto startResult = application.domain().start(QByteArray());
	if (startResult != Storage::StartResult::Success) {
		const auto errorWritten = WriteLine(QStringLiteral("read-error:startup"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto account = SelectedAccount(application.domain());
	if (account == nullptr) {
		const auto errorWritten = WriteLine(
			QStringLiteral("read-error:invalid-account-index"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto session = WaitForSession(*account);
	if (session == nullptr) {
		const auto errorWritten = WriteLine(
			QStringLiteral("read-error:session-timeout"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	if (!WaitForDialogs(*session)) {
		const auto errorWritten = WriteLine(
			QStringLiteral("read-error:dialogs-timeout"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto stableId = cConsoleReadPeerId().trimmed();
	const auto peerId = ParseStablePeerId(stableId);
	const auto history = ResolveLoadedHistory(*session, stableId);
	if (history == nullptr) {
		const auto errorWritten = WriteLine(QStringLiteral("read-error:invalid-peer"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto around = ParseReadCursor(
		cConsoleReadCursor().trimmed(),
		*peerId,
		stableId);
	if (!around) {
		const auto errorWritten = WriteLine(QStringLiteral("read-error:invalid-cursor"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto output = WaitForPage(*session, history, stableId, *around);
	if (!output) {
		const auto errorWritten = WriteLine(
			QStringLiteral("read-error:history-no-progress"));
		const auto doneWritten = WriteLine(QStringLiteral("read-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	if (!WriteRead(*output)) {
		return 1;
	}
	if (!WriteLine(QStringLiteral("read-mode:done"))) {
		return 1;
	}
	return 0;
}

} // namespace TgCli::Hosted