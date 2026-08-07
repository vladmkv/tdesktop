#include "../capabilities/domain_lifecycle_capabilities.h"

#include <memory>

#include <rpl/never.h>

#include "../capabilities/account_network_capabilities.h"
#include "../capabilities/session_service_capabilities.h"
#include "../capabilities/storage_settings_capabilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_account.h"
#include "mtproto/mtproto_config.h"

namespace TgCli::Capabilities {
namespace {

class HostedConsoleAccountNetworkCapabilities final
	: public AccountNetworkCapabilities {
public:
	[[nodiscard]] std::unique_ptr<MTP::Config> fallbackProductionConfigCopy() const override {
		return std::make_unique<MTP::Config>(Core::App().fallbackProductionConfig());
	}

	[[nodiscard]] rpl::producer<ProxyChange> proxyChanges() const override {
		return rpl::never<ProxyChange>();
	}

	void notifyProxyConnectionTypeChanged() override {
	}

	void checkProxyRotation(Main::Account *, int) override {
	}
};

class HostedConsoleDomainLifecycleCapabilities final
	: public DomainLifecycleCapabilities {
public:
	void startSettingsAndBackground() override {
		Core::App().startSettingsAndBackground();
	}

	void createNotificationsManager() override {
	}

	void runOnMain(FnMut<void()> &&callback) override {
		crl::on_main(&Core::App(), std::move(callback));
	}

	void postponeCall(FnMut<void()> &&callback) override {
		Core::App().postponeCall(std::move(callback));
	}

	[[nodiscard]] std::vector<uint64> accountsOrder() const override {
		return Core::App().settings().accountsOrder();
	}

	[[nodiscard]] std::unique_ptr<MTP::Config> fallbackProductionConfigCopy() const override {
		return std::make_unique<MTP::Config>(Core::App().fallbackProductionConfig());
	}

	void refreshFallbackProductionConfig(const MTP::Config &config) override {
		Core::App().refreshFallbackProductionConfig(config);
	}

	[[nodiscard]] bool mainMenuAccountsShown() const override {
		return Core::App().settings().mainMenuAccountsShown();
	}

	void setMainMenuAccountsShown(bool shown) override {
		Core::App().settings().setMainMenuAccountsShown(shown);
	}

	void saveSettingsDelayed(crl::time delay) override {
		if (delay > 0) {
			Core::App().saveSettingsDelayed(delay);
			return;
		}
		Core::App().saveSettingsDelayed();
	}

	[[nodiscard]] bool keepAccountWithoutSession(Main::Account *) const override {
		return true;
	}

	[[nodiscard]] Window::Controller *separateWindowFor(Main::Account *) const override {
		return nullptr;
	}

	Window::Controller *ensureSeparateWindowFor(Main::Account *) override {
		return nullptr;
	}

	[[nodiscard]] bool passcodeLocked() const override {
		return Core::App().passcodeLocked();
	}

	void unlockPasscode() override {
		Core::App().unlockPasscode();
	}

	void setSystemUnlockEnabled(bool enabled) override {
		Core::App().settings().setSystemUnlockEnabled(enabled);
	}

	void preventOrInvoke(Fn<void()> &&callback) override {
		if (callback) {
			callback();
		}
	}
};

class HostedConsoleStorageSettingsCapabilities final
	: public StorageSettingsCapabilities {
public:
	[[nodiscard]] bool isNightMode() const override {
		return false;
	}

	void setBackgroundTileValues(bool, bool) override {
	}

	[[nodiscard]] QByteArray tonsiteStorageToken() const override {
		return QByteArray();
	}

	void setTonsiteStorageToken(const QByteArray &) override {
	}

	void saveSettingsDelayed() override {
	}
};

class HostedConsoleSessionServiceCapabilities final
	: public SessionServiceCapabilities {
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

class HostedConsoleDomainAccountFactoryCapabilities final
	: public DomainAccountFactoryCapabilities {
public:
	[[nodiscard]] AccountCapabilityBundle createAccountCapabilityBundle() override {
		return {
			.network = std::make_unique<HostedConsoleAccountNetworkCapabilities>(),
			.storage = std::make_unique<HostedConsoleStorageSettingsCapabilities>(),
			.session = std::make_unique<HostedConsoleSessionServiceCapabilities>(),
		};
	}
};

} // namespace

DomainCapabilityBundle CreateHostedConsoleDomainCapabilityBundle() {
	return {
		.lifecycle = std::make_unique<HostedConsoleDomainLifecycleCapabilities>(),
		.accountFactory = std::make_unique<HostedConsoleDomainAccountFactoryCapabilities>(),
	};
}

} // namespace TgCli::Capabilities