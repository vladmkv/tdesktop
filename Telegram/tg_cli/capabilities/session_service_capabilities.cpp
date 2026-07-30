#include "session_service_capabilities.h"

#include <memory>

#include "core/application.h"
#include "data/data_download_manager.h"
#include "main/main_session.h"

namespace TgCli::Capabilities {
namespace {

class DesktopSessionServiceCapabilities final : public SessionServiceCapabilities {
public:
	void lockBySetupEmail() override {
		Core::App().lockBySetupEmail();
	}

	void unlockSetupEmail() override {
		Core::App().unlockSetupEmail();
	}

	void trackDownloadSession(Main::Session *session) override {
		if (!session) {
			return;
		}
		Core::App().downloadManager().trackSession(session);
	}

	[[nodiscard]] Window::Controller *windowForPeer(PeerData *peer) const override {
		if (!peer) {
			return Core::App().activePrimaryWindow();
		}
		return Core::App().windowFor(not_null{ peer });
	}

	[[nodiscard]] Window::Controller *activePrimaryWindow() const override {
		return Core::App().activePrimaryWindow();
	}
};

} // namespace

[[nodiscard]] std::unique_ptr<SessionServiceCapabilities> CreateDesktopSessionServiceCapabilities() {
	return std::make_unique<DesktopSessionServiceCapabilities>();
}


} // namespace TgCli::Capabilities
