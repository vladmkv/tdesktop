#pragma once

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

[[nodiscard]] int RunHostedConsoleAccountsMode(Core::Application &application);

} // namespace TgCli::Hosted
