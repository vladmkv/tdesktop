#include "hosted_console_status_writer.h"

#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>

#include <cstdio>

namespace TgCli::Hosted {
namespace {

QString ConsoleStatusFilePath() {
	auto path = cConsoleLogPath();
	if (path.isEmpty()) {
		path = cWorkingDir() + QStringLiteral("tdata/console_bootstrap.log");
	}
	return path;
}

} // namespace

HostedConsoleStatusWriteResult WriteHostedConsoleStatusLine(QStringView line) {
	HostedConsoleStatusWriteResult result;
	const auto path = ConsoleStatusFilePath();
	result.path = path;
	if (!path.isEmpty()) {
		auto directory = QFileInfo(path).absoluteDir();
		if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
			result.failure = HostedConsoleStatusWriteFailure::MkdirFailed;
			return result;
		}
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
			result.failure = HostedConsoleStatusWriteFailure::OpenFailed;
			return result;
		}
		QTextStream out(&file);
		out << line << Qt::endl;
		if (out.status() != QTextStream::Ok) {
			result.failure = HostedConsoleStatusWriteFailure::WriteFailed;
			return result;
		}
		if (!file.flush()) {
			result.failure = HostedConsoleStatusWriteFailure::FlushFailed;
			return result;
		}
	}

	auto stream = QTextStream(stdout, QIODevice::WriteOnly);
	stream.setCodec("UTF-8");
	stream << line << Qt::endl;
	result.ok = true;
	return result;
}

} // namespace TgCli::Hosted