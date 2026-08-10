#pragma once

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

[[nodiscard]] int RunHostedConsoleChatsMode(Core::Application &application);

} // namespace TgCli::Hosted