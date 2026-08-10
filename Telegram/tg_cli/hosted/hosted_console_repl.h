#pragma once

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

[[nodiscard]] int RunHostedConsoleRepl(Core::Application &application);

} // namespace TgCli::Hosted