#include "hosted_console_repl.h"

#include "hosted_console_command_dispatcher.h"
#include "hosted_console_status_writer.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

#include <csignal>

#include <QtCore/QCoreApplication>
#include <QtCore/QRegularExpression>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>

namespace TgCli::Hosted {
namespace {

volatile std::sig_atomic_t Interrupted = 0;

void HandleInterrupt(int) {
	Interrupted = 1;
}

[[nodiscard]] bool WriteLine(const QString &line) {
	return WriteHostedConsoleStatusLine(line).ok;
}

[[nodiscard]] QStringList CommandTokens(const QString &line) {
	return line.trimmed().split(
		QRegularExpression(QStringLiteral("\\s+")),
		Qt::SkipEmptyParts);
}

} // namespace

int RunHostedConsoleRepl(Core::Application &application) {
	std::signal(SIGINT, HandleInterrupt);
	if (!application.domain().started()
		&& application.domain().start(QByteArray()) != Storage::StartResult::Success) {
		const auto written = WriteLine(QStringLiteral("repl-error:startup"));
		(void)written;
		return 1;
	}
	if (!WriteLine(QStringLiteral("repl-started"))) {
		return 1;
	}

	auto interruptTimer = QTimer();
	interruptTimer.setInterval(50);
	QObject::connect(&interruptTimer, &QTimer::timeout, [] {
		if (Interrupted) {
			QCoreApplication::exit(130);
		}
	});
	interruptTimer.start();

	auto input = QTextStream(stdin, QIODevice::ReadOnly);
	input.setCodec("UTF-8");
	auto context = HostedConsoleCommandContext();
	while (!Interrupted) {
		if (!WriteLine(QStringLiteral("repl-prompt"))) {
			return 1;
		}
		const auto line = input.readLine();
		if (line.isNull()) {
			const auto written = WriteLine(QStringLiteral("repl-eof"));
			(void)written;
			return 0;
		}
		const auto parsed = ParseHostedConsoleCommand(CommandTokens(line));
		if (!parsed.request) {
			const auto written = WriteLine(
				QStringLiteral("command-error:") + parsed.error);
			(void)written;
			continue;
		}
		const auto result = RunHostedConsoleCommand(
			application,
			context,
			*parsed.request);
		if (result.quitRequested) {
			return result.exitCode;
		}
	}
	const auto written = WriteLine(QStringLiteral("repl-interrupted"));
	(void)written;
	return 130;
}

} // namespace TgCli::Hosted