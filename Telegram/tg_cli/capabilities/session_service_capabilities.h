#pragma once

#include <memory>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class Controller;
} // namespace Window

namespace TgCli::Capabilities {

class SessionServiceCapabilities {
public:
	virtual ~SessionServiceCapabilities() = default;

	virtual void lockBySetupEmail() = 0;
	virtual void unlockSetupEmail() = 0;
	virtual void trackDownloadSession(Main::Session *session) = 0;
	[[nodiscard]] virtual Window::Controller *windowForPeer(PeerData *peer) const = 0;
	[[nodiscard]] virtual Window::Controller *activePrimaryWindow() const = 0;
};

[[nodiscard]] std::unique_ptr<SessionServiceCapabilities> CreateDesktopSessionServiceCapabilities();

} // namespace TgCli::Capabilities
