#pragma once

#include <memory>
#include <rpl/producer.h>

#include "mtproto/mtproto_proxy_data.h"

namespace Main {
class Account;
} // namespace Main

namespace MTP {
class Config;
} // namespace MTP

namespace TgCli::Capabilities {

struct ProxyChange {
	MTP::ProxyData was;
	MTP::ProxyData now;
};

class AccountNetworkCapabilities {
public:
	virtual ~AccountNetworkCapabilities() = default;

	[[nodiscard]] virtual std::unique_ptr<MTP::Config> fallbackProductionConfigCopy() const = 0;
	[[nodiscard]] virtual rpl::producer<ProxyChange> proxyChanges() const = 0;
	virtual void notifyProxyConnectionTypeChanged() = 0;
	virtual void checkProxyRotation(Main::Account *account, int state) = 0;
};

[[nodiscard]] std::unique_ptr<AccountNetworkCapabilities> CreateDesktopAccountNetworkCapabilities();

} // namespace TgCli::Capabilities
