#pragma once

#include <QtCore/QString>

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

struct HostedConsoleReadModeResult {
	int exitCode = 0;
	QString nextCursor;
};

[[nodiscard]] HostedConsoleReadModeResult RunHostedConsoleReadMode(
	Core::Application &application);

} // namespace TgCli::Hosted