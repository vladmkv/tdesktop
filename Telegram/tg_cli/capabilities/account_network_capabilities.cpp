#include "account_network_capabilities.h"

#include <memory>

#include <rpl/map.h>

#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_account.h"
#include "mtproto/mtproto_config.h"

namespace TgCli::Capabilities {
namespace {

class DesktopAccountNetworkCapabilities final : public AccountNetworkCapabilities {
public:
	[[nodiscard]] std::unique_ptr<MTP::Config> fallbackProductionConfigCopy() const override {
		return std::make_unique<MTP::Config>(Core::App().fallbackProductionConfig());
	}

	[[nodiscard]] rpl::producer<ProxyChange> proxyChanges() const override {
		return Core::App().proxyChanges() | rpl::map([](const Core::Application::ProxyChange &change) {
			return ProxyChange{
				.was = change.was,
				.now = change.now,
			};
		});
	}

	void notifyProxyConnectionTypeChanged() override {
		Core::App().settings().proxy().connectionTypeChangesNotify();
	}

	void checkProxyRotation(Main::Account *account, int state) override {
		if (!account) {
			return;
		}
		Core::App().checkProxyRotation(not_null{ account }, state);
	}
};

} // namespace

std::unique_ptr<AccountNetworkCapabilities> CreateDesktopAccountNetworkCapabilities() {
	return std::make_unique<DesktopAccountNetworkCapabilities>();
}

} // namespace TgCli::Capabilities
