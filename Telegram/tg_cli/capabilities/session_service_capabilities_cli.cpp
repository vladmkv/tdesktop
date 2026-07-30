#include "session_service_capabilities.h"

#include <memory>

namespace TgCli::Capabilities {
namespace {

class CliSessionServiceCapabilities final : public SessionServiceCapabilities {
public:
	void lockBySetupEmail() override {
	}

	void unlockSetupEmail() override {
	}

	void trackDownloadSession(Main::Session *) override {
	}

	[[nodiscard]] Window::Controller *windowForPeer(PeerData *) const override {
		return nullptr;
	}

	[[nodiscard]] Window::Controller *activePrimaryWindow() const override {
		return nullptr;
	}
};

} // namespace

std::unique_ptr<SessionServiceCapabilities> CreateCliSessionServiceCapabilities() {
	return std::make_unique<CliSessionServiceCapabilities>();
}

} // namespace TgCli::Capabilities
