#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "base/timer.h"

namespace Main {
class Account;
} // namespace Main

namespace MTP {
class Config;
} // namespace MTP

namespace Window {
class Controller;
} // namespace Window

namespace TgCli::Capabilities {

class DomainLifecycleCapabilities {
public:
	virtual ~DomainLifecycleCapabilities() = default;

	virtual void startSettingsAndBackground() = 0;
	virtual void createNotificationsManager() = 0;
	virtual void runOnMain(std::function<void()> callback) = 0;
	virtual void postponeCall(std::function<void()> callback) = 0;
	[[nodiscard]] virtual std::vector<uint64> accountsOrder() const = 0;
	[[nodiscard]] virtual std::unique_ptr<MTP::Config> fallbackProductionConfigCopy() const = 0;
	virtual void refreshFallbackProductionConfig(const MTP::Config &config) = 0;
	[[nodiscard]] virtual bool mainMenuAccountsShown() const = 0;
	virtual void setMainMenuAccountsShown(bool shown) = 0;
	virtual void saveSettingsDelayed(crl::time delay = 0) = 0;
	[[nodiscard]] virtual Window::Controller *separateWindowFor(Main::Account *account) const = 0;
	virtual Window::Controller *ensureSeparateWindowFor(Main::Account *account) = 0;
	[[nodiscard]] virtual bool passcodeLocked() const = 0;
	virtual void unlockPasscode() = 0;
	virtual void setSystemUnlockEnabled(bool enabled) = 0;
	virtual void preventOrInvoke(std::function<void()> callback) = 0;
};

[[nodiscard]] std::unique_ptr<DomainLifecycleCapabilities> CreateDesktopDomainLifecycleCapabilities();

} // namespace TgCli::Capabilities
