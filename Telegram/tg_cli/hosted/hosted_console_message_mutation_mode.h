#pragma once

#include <QtCore/QString>

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

struct HostedConsoleMessageMutationRequest {
	bool edit = false;
	QString chatId;
	int messageId = 0;
	QString text;
};

[[nodiscard]] int RunHostedConsoleMessageMutation(
	Core::Application &application,
	const HostedConsoleMessageMutationRequest &request);

} // namespace TgCli::Hosted