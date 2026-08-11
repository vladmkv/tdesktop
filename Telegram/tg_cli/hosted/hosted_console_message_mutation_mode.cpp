#include "hosted_console_message_mutation_mode.h"

#include <optional>

#include "hosted_console_message_mutation_format.h"
#include "hosted_console_status_writer.h"
#include "settings.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_histories.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
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

enum class MutationResultKind {
	Acknowledged,
	InvalidPeer,
	InvalidMessage,
	PermissionDenied,
	TimeWindow,
	NetworkOrServer,
	Timeout,
};

struct MutationResult {
	MutationResultKind kind = MutationResultKind::Timeout;
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

[[nodiscard]] MutationResultKind ErrorKind(const QString &error) {
	if (error == QStringLiteral("MESSAGE_EDIT_TIME_EXPIRED")
		|| error == QStringLiteral("MESSAGE_DELETE_TIME_EXPIRED")) {
		return MutationResultKind::TimeWindow;
	}
	if (error == QStringLiteral("MESSAGE_ID_INVALID")
		|| error == QStringLiteral("PEER_ID_INVALID")) {
		return MutationResultKind::InvalidMessage;
	}
	if (error == QStringLiteral("CHAT_ADMIN_REQUIRED")
		|| error == QStringLiteral("MESSAGE_AUTHOR_REQUIRED")
		|| error == QStringLiteral("MESSAGE_DELETE_FORBIDDEN")) {
		return MutationResultKind::PermissionDenied;
	}
	return MutationResultKind::NetworkOrServer;
}

[[nodiscard]] MutationResult EditText(
		HistoryItem &item,
		const QString &text) {
	auto result = std::optional<MutationResult>();
	auto loop = QEventLoop();
	auto timer = QTimer();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	Api::EditTextMessage(
		not_null{ &item },
		TextWithEntities{ .text = text },
		Data::WebPageDraft(),
		Api::SendOptions(),
		[&](mtpRequestId) {
			result = MutationResult{ .kind = MutationResultKind::Acknowledged };
			loop.quit();
		},
		[&](const QString &error, mtpRequestId) {
			result = MutationResult{ .kind = ErrorKind(error), .error = error };
			loop.quit();
		},
		false);
	timer.start(30000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	timer.stop();
	return result.value_or(MutationResult{});
}

[[nodiscard]] MutationResult DeleteMessage(
		Main::Session &session,
		not_null<History*> history,
		MsgId messageId,
		bool revoke) {
	auto result = std::optional<MutationResult>();
	auto loop = QEventLoop();
	auto timer = QTimer();
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	session.data().histories().deleteMessages(
		history,
		QVector<MTPint>{ MTP_int(messageId) },
		revoke,
		[&] {
			result = MutationResult{ .kind = MutationResultKind::Acknowledged };
			loop.quit();
		},
		[&](const MTP::Error &error) {
			result = MutationResult{
				.kind = ErrorKind(error.type()),
				.error = error.type(),
			};
			loop.quit();
		});
	timer.start(30000);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
	timer.stop();
	return result.value_or(MutationResult{});
}

[[nodiscard]] QString ResultName(MutationResultKind kind) {
	switch (kind) {
	case MutationResultKind::PermissionDenied:
		return QStringLiteral("permission-denied");
	case MutationResultKind::TimeWindow:
		return QStringLiteral("time-window");
	case MutationResultKind::NetworkOrServer:
		return QStringLiteral("network-or-server");
	case MutationResultKind::Timeout:
		return QStringLiteral("timeout");
	case MutationResultKind::InvalidPeer:
		return QStringLiteral("invalid-peer");
	case MutationResultKind::InvalidMessage:
		return QStringLiteral("invalid-message");
	case MutationResultKind::Acknowledged:
		return QStringLiteral("acknowledged");
	}
	Unexpected("Hosted console mutation result.");
}

} // namespace

int RunHostedConsoleMessageMutation(
		Core::Application &application,
		const HostedConsoleMessageMutationRequest &request) {
	const auto operation = request.edit
		? QStringLiteral("edit")
		: QStringLiteral("delete");
	if (!WriteLine(operation + QStringLiteral("-mode:started"))) {
		return 1;
	}
	const auto startResult = application.domain().started()
		? Storage::StartResult::Success
		: application.domain().start(QByteArray());
	if (startResult != Storage::StartResult::Success) {
		const auto written = WriteLine(operation + QStringLiteral("-error:startup"));
		(void)written;
		return 1;
	}
	const auto account = SelectedAccount(application.domain());
	if (account == nullptr) {
		const auto written = WriteLine(
			operation + QStringLiteral("-error:invalid-account-index"));
		(void)written;
		return 1;
	}
	const auto session = WaitForSession(*account);
	if (session == nullptr) {
		const auto written = WriteLine(
			operation + QStringLiteral("-error:session-timeout"));
		(void)written;
		return 1;
	}
	if (!WaitForDialogs(*session)) {
		const auto written = WriteLine(
			operation + QStringLiteral("-error:dialogs-timeout"));
		(void)written;
		return 1;
	}
	const auto peerId = ParseStablePeerId(request.chatId);
	const auto peer = peerId ? session->data().peerLoaded(*peerId) : nullptr;
	if (peer == nullptr) {
		const auto written = WriteLine(
			operation + QStringLiteral("-error:invalid-peer"));
		(void)written;
		return 1;
	}
	const auto item = session->data().message(*peerId, MsgId(request.messageId));
	if (item == nullptr) {
		const auto written = WriteLine(
			operation + QStringLiteral("-error:invalid-message"));
		(void)written;
		return 1;
	}
	const auto now = base::unixtime::now();
	MutationResult result;
	if (request.edit) {
		if (!item->canBeEdited()) {
			result.kind = MutationResultKind::PermissionDenied;
		} else if (item->media() || !item->allowsEdit(now)) {
			result.kind = MutationResultKind::TimeWindow;
		} else {
			result = EditText(*item, request.text);
		}
	} else if (!item->canDelete()) {
		result.kind = MutationResultKind::PermissionDenied;
	} else if (!item->history()->peer->isSelf()
		&& !item->history()->peer->isChannel()
		&& !item->canDeleteForEveryone(now)) {
		result.kind = MutationResultKind::TimeWindow;
	} else {
		result = DeleteMessage(
			*session,
			item->history(),
			item->id,
			!item->history()->peer->isSelf());
	}
	if (result.kind == MutationResultKind::Acknowledged) {
		const auto output = HostedConsoleMessageMutationOutputData{
			.operation = operation,
			.chatId = request.chatId,
			.messageId = request.messageId,
		};
		if (IsConsoleJsonFormat()) {
			const auto written = WriteLine(HostedConsoleMessageMutationJsonLine(output));
			(void)written;
		}
		const auto written = WriteLine(operation + QStringLiteral("-acknowledged:") + request.chatId
			+ QStringLiteral(":") + QString::number(request.messageId));
		(void)written;
		return 0;
	}
	const auto written = WriteLine(operation + QStringLiteral("-error:") + ResultName(result.kind)
		+ (result.error.isEmpty() ? QString() : QStringLiteral(":") + result.error));
	(void)written;
	return 1;
}

} // namespace TgCli::Hosted