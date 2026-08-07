#include "domain_lifecycle_capabilities.h"

#include <memory>

#include "account_network_capabilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_account.h"
#include "mtproto/mtproto_config.h"
#include "session_service_capabilities.h"
#include "storage_settings_capabilities.h"
#include "window/notifications_manager.h"

namespace TgCli::Capabilities {
namespace {

class DesktopDomainLifecycleCapabilities final : public DomainLifecycleCapabilities {
public:
	void startSettingsAndBackground() override {
		Core::App().startSettingsAndBackground();
	}

	void createNotificationsManager() override {
		Core::App().notifications().createManager();
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
		return false;
	}

	[[nodiscard]] Window::Controller *separateWindowFor(Main::Account *account) const override {
		if (!account) {
			return nullptr;
		}
		return Core::App().separateWindowFor(not_null{ account });
	}

	Window::Controller *ensureSeparateWindowFor(Main::Account *account) override {
		if (!account) {
			return nullptr;
		}
		return Core::App().ensureSeparateWindowFor(not_null{ account });
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
		Core::App().preventOrInvoke(std::move(callback));
	}
};

class DesktopDomainAccountFactoryCapabilities final : public DomainAccountFactoryCapabilities {
public:
	[[nodiscard]] AccountCapabilityBundle createAccountCapabilityBundle() override {
		return {
			.network = CreateDesktopAccountNetworkCapabilities(),
			.storage = CreateDesktopStorageSettingsCapabilities(),
			.session = CreateDesktopSessionServiceCapabilities(),
		};
	}
};

} // namespace

std::unique_ptr<DomainLifecycleCapabilities> CreateDesktopDomainLifecycleCapabilities() {
	return std::make_unique<DesktopDomainLifecycleCapabilities>();
}

DomainCapabilityBundle CreateDesktopDomainCapabilityBundle() {
	return {
		.lifecycle = CreateDesktopDomainLifecycleCapabilities(),
		.accountFactory = std::make_unique<DesktopDomainAccountFactoryCapabilities>(),
	};
}

} // namespace TgCli::Capabilities
