#pragma once

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

[[nodiscard]] int RunHostedConsoleSendMode(Core::Application &application);

} // namespace TgCli::Hosted