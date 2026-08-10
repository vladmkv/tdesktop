#pragma once

namespace Core {
class Application;
} // namespace Core

namespace TgCli::Hosted {

[[nodiscard]] int RunHostedConsoleReadMode(Core::Application &application);

} // namespace TgCli::Hosted