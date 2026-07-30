#include "domain_lifecycle_capabilities.h"

#include <memory>

#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_account.h"
#include "mtproto/mtproto_config.h"
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

	void runOnMain(std::function<void()> callback) override {
		crl::on_main(&Core::App(), [callback = std::move(callback)]() mutable {
			callback();
		});
	}

	void postponeCall(std::function<void()> callback) override {
		Core::App().postponeCall([callback = std::move(callback)]() mutable {
			callback();
		});
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

	void preventOrInvoke(std::function<void()> callback) override {
		Core::App().preventOrInvoke([callback = std::move(callback)]() mutable {
			callback();
		});
	}
};

} // namespace

std::unique_ptr<DomainLifecycleCapabilities> CreateDesktopDomainLifecycleCapabilities() {
	return std::make_unique<DesktopDomainLifecycleCapabilities>();
}

} // namespace TgCli::Capabilities
