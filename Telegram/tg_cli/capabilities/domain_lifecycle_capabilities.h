#pragma once

#include <memory>
#include <vector>

#include "base/basic_types.h"
#include <crl/crl_time.h>

#include "account_network_capabilities.h"
#include "session_service_capabilities.h"
#include "storage_settings_capabilities.h"

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
	// TG_CHANGE_BEGIN: domain-capability-callback-signatures
	virtual void runOnMain(FnMut<void()> &&callback) = 0;
	virtual void postponeCall(FnMut<void()> &&callback) = 0;
	[[nodiscard]] virtual std::vector<uint64> accountsOrder() const = 0;
	[[nodiscard]] virtual std::unique_ptr<MTP::Config> fallbackProductionConfigCopy() const = 0;
	virtual void refreshFallbackProductionConfig(const MTP::Config &config) = 0;
	[[nodiscard]] virtual bool mainMenuAccountsShown() const = 0;
	virtual void setMainMenuAccountsShown(bool shown) = 0;
	virtual void saveSettingsDelayed(crl::time delay = 0) = 0;
	[[nodiscard]] virtual bool keepAccountWithoutSession(Main::Account *account) const = 0;
	[[nodiscard]] virtual Window::Controller *separateWindowFor(Main::Account *account) const = 0;
	virtual Window::Controller *ensureSeparateWindowFor(Main::Account *account) = 0;
	[[nodiscard]] virtual bool passcodeLocked() const = 0;
	virtual void unlockPasscode() = 0;
	virtual void setSystemUnlockEnabled(bool enabled) = 0;
	virtual void preventOrInvoke(Fn<void()> &&callback) = 0;
	// TG_CHANGE_END: domain-capability-callback-signatures
};

// TG_CHANGE_BEGIN: domain-capability-bundle-types
struct AccountCapabilityBundle {
	std::unique_ptr<AccountNetworkCapabilities> network;
	std::unique_ptr<StorageSettingsCapabilities> storage;
	std::unique_ptr<SessionServiceCapabilities> session;
};

class DomainAccountFactoryCapabilities {
public:
	virtual ~DomainAccountFactoryCapabilities() = default;
	[[nodiscard]] virtual AccountCapabilityBundle createAccountCapabilityBundle() = 0;
};

struct DomainCapabilityBundle {
	std::unique_ptr<DomainLifecycleCapabilities> lifecycle;
	std::unique_ptr<DomainAccountFactoryCapabilities> accountFactory;
};
// TG_CHANGE_END: domain-capability-bundle-types

[[nodiscard]] std::unique_ptr<DomainLifecycleCapabilities> CreateDesktopDomainLifecycleCapabilities();
// TG_CHANGE_BEGIN: domain-capability-bundle-factories
[[nodiscard]] DomainCapabilityBundle CreateDesktopDomainCapabilityBundle();
// TG_CHANGE_END: domain-capability-bundle-factories
// TG_CHANGE_BEGIN: domain-hosted-console-bundle-factory
[[nodiscard]] DomainCapabilityBundle CreateHostedConsoleDomainCapabilityBundle();
// TG_CHANGE_END: domain-hosted-console-bundle-factory

} // namespace TgCli::Capabilities
