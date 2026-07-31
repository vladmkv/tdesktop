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

void WriteHostedConsoleStatusLine(QStringView line) {
	const auto path = ConsoleStatusFilePath();
	if (!path.isEmpty()) {
		QFileInfo(path).absoluteDir().mkpath(QStringLiteral("."));
		auto file = QFile(path);
		if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
			QTextStream out(&file);
			out << line << Qt::endl;
		}
	}

	auto stream = QTextStream(stdout, QIODevice::WriteOnly);
	stream << line << Qt::endl;
}

} // namespace TgCli::Hosted