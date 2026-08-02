#pragma once

#include <QtCore/QString>

namespace Main {
class Domain;
} // namespace Main

namespace TgCli::Hosted {

[[nodiscard]] QString HostedConsoleProfileSnapshotStatusLine(Main::Domain &domain);

} // namespace TgCli::Hosted
