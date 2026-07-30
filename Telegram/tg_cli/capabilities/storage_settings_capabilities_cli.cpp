#include "storage_settings_capabilities.h"

#include <memory>

namespace TgCli::Capabilities {
namespace {

class CliStorageSettingsCapabilities final : public StorageSettingsCapabilities {
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

} // namespace

std::unique_ptr<StorageSettingsCapabilities> CreateCliStorageSettingsCapabilities() {
	return std::make_unique<CliStorageSettingsCapabilities>();
}

} // namespace TgCli::Capabilities
