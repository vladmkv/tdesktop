#include "hosted_console_send_mode.h"

#include <optional>

#include "hosted_console_send_format.h"
#include "hosted_console_status_writer.h"
#include "settings.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/random.h"
#include "core/application.h"
#include "data/data_chat_participant_status.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "storage/storage_domain.h"

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>

#include <rpl/rpl.h>

namespace TgCli::Hosted {
namespace {

enum class SendResultKind {
	Acknowledged,
	InvalidPeer,
	PermissionDenied,
	NetworkOrServer,
	Timeout,
};

struct SendResult {
	SendResultKind kind = SendResultKind::Timeout;
	int messageId = 0;
	QString error;
};

[[nodiscard]] bool WriteLine(const QString &line) {
	return WriteHostedConsoleStatusLine(line).ok;
}

[[nodiscard]] bool IsConsoleJsonFormat() {
	return cConsoleFormat().trimmed().toLower() == QStringLiteral("json");
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

[[nodiscard]] SendResult SendText(
		Main::Session &session,
		not_null<History*> history,
		const QString &text) {
	if (!Data::CanSendTexts(history->peer)) {
		return { .kind = SendResultKind::PermissionDenied };
	}
	const auto peer = history->peer;
	const auto randomId = base::RandomValue<uint64>();
	auto result = std::optional<SendResult>();
	auto loop = QEventLoop();
	auto timer = QTimer();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	session.data().histories().sendPreparedMessage(
		history,
		FullReplyTo(),
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMessage>(
			MTP_flags(MTPmessages_SendMessage::Flags(0)),
			peer->input(),
			Data::Histories::ReplyToPlaceholder(),
			MTP_string(text),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(0),
			MTP_int(0),
			MTP_inputPeerEmpty(),
			Data::ShortcutIdToMTP(&session, BusinessShortcutId()),
			MTP_long(0),
			MTP_long(0),
			MTPSuggestedPost(),
			MTPInputRichMessage()
		), [&] (const MTPUpdates &, const MTP::Response &) {
			result = SendResult{ .kind = SendResultKind::Acknowledged };
			loop.quit();
		}, [&] (const MTP::Error &error, const MTP::Response &) {
			result = SendResult{
				.kind = SendResultKind::NetworkOrServer,
				.error = error.type(),
			};
			loop.quit();
		});
	timer.start(30000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	timer.stop();
	return result.value_or(SendResult{});
}

} // namespace

int RunHostedConsoleSendMode(Core::Application &application) {
	if (!WriteLine(QStringLiteral("send-mode:started"))) {
		return 1;
	}
	const auto startResult = application.domain().started()
		? Storage::StartResult::Success
		: application.domain().start(QByteArray());
	if (startResult != Storage::StartResult::Success) {
		const auto errorWritten = WriteLine(QStringLiteral("send-error:startup"));
		const auto doneWritten = WriteLine(QStringLiteral("send-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto account = SelectedAccount(application.domain());
	if (account == nullptr) {
		const auto errorWritten = WriteLine(
			QStringLiteral("send-error:invalid-account-index"));
		const auto doneWritten = WriteLine(QStringLiteral("send-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto session = WaitForSession(*account);
	if (session == nullptr) {
		const auto errorWritten = WriteLine(
			QStringLiteral("send-error:session-timeout"));
		const auto doneWritten = WriteLine(QStringLiteral("send-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	if (!WaitForDialogs(*session)) {
		const auto errorWritten = WriteLine(
			QStringLiteral("send-error:dialogs-timeout"));
		const auto doneWritten = WriteLine(QStringLiteral("send-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto stableId = cConsoleReadPeerId().trimmed();
	const auto peerId = ParseStablePeerId(stableId);
	const auto peer = peerId ? session->data().peerLoaded(*peerId) : nullptr;
	if (peer == nullptr) {
		const auto errorWritten = WriteLine(QStringLiteral("send-error:invalid-peer"));
		const auto doneWritten = WriteLine(QStringLiteral("send-mode:done"));
		(void)errorWritten;
		(void)doneWritten;
		return 1;
	}
	const auto result = SendText(
		*session,
		session->data().history(*peerId),
		cConsoleSendText());
	switch (result.kind) {
	case SendResultKind::Acknowledged: {
		const auto output = HostedConsoleSendOutputData{ .chatId = stableId };
		if (IsConsoleJsonFormat() && !WriteLine(HostedConsoleSendJsonLine(output))) {
			return 1;
		}
		if (!WriteLine(QStringLiteral("send-acknowledged:") + stableId)) {
			return 1;
		}
		break;
	}
	case SendResultKind::PermissionDenied:
		{
			const auto errorWritten = WriteLine(
				QStringLiteral("send-error:permission-denied"));
			(void)errorWritten;
		}
		break;
	case SendResultKind::NetworkOrServer:
		{
			const auto errorWritten = WriteLine(
				QStringLiteral("send-error:network-or-server:") + result.error);
			(void)errorWritten;
		}
		break;
	case SendResultKind::Timeout:
		{
			const auto errorWritten = WriteLine(QStringLiteral("send-error:timeout"));
			(void)errorWritten;
		}
		break;
	case SendResultKind::InvalidPeer:
		Unexpected("Invalid peer handled before sending.");
	}
	const auto doneWritten = WriteLine(QStringLiteral("send-mode:done"));
	(void)doneWritten;
	return (result.kind == SendResultKind::Acknowledged) ? 0 : 1;
}

} // namespace TgCli::Hosted