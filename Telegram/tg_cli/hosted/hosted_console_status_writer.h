#pragma once

#include <QtCore/QString>
#include <QtCore/QStringView>

namespace TgCli::Hosted {

enum class HostedConsoleStatusWriteFailure {
	None = 0,
	MkdirFailed,
	OpenFailed,
	WriteFailed,
	FlushFailed,
};

struct HostedConsoleStatusWriteResult {
	bool ok = false;
	HostedConsoleStatusWriteFailure failure = HostedConsoleStatusWriteFailure::None;
	QString path;
};

[[nodiscard]] HostedConsoleStatusWriteResult WriteHostedConsoleStatusLine(QStringView line);

} // namespace TgCli::Hosted