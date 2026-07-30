#pragma once

#include <memory>

#include <QByteArray>

namespace TgCli::Capabilities {

class StorageSettingsCapabilities {
public:
	virtual ~StorageSettingsCapabilities() = default;

	[[nodiscard]] virtual bool isNightMode() const = 0;
	virtual void setBackgroundTileValues(bool dayTile, bool nightTile) = 0;
	[[nodiscard]] virtual QByteArray tonsiteStorageToken() const = 0;
	virtual void setTonsiteStorageToken(const QByteArray &token) = 0;
	virtual void saveSettingsDelayed() = 0;
};

[[nodiscard]] std::unique_ptr<StorageSettingsCapabilities> CreateDesktopStorageSettingsCapabilities();
[[nodiscard]] std::unique_ptr<StorageSettingsCapabilities> CreateCliStorageSettingsCapabilities();

} // namespace TgCli::Capabilities
