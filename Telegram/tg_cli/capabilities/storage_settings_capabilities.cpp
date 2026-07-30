#include "storage_settings_capabilities.h"

#include <memory>

#include "core/application.h"
#include "core/core_settings.h"
#include "window/themes/window_theme.h"

namespace TgCli::Capabilities {
namespace {

class DesktopStorageSettingsCapabilities final : public StorageSettingsCapabilities {
public:
	[[nodiscard]] bool isNightMode() const override {
		return Window::Theme::IsNightMode();
	}

	void setBackgroundTileValues(bool dayTile, bool nightTile) override {
		Window::Theme::Background()->setTileDayValue(dayTile);
		Window::Theme::Background()->setTileNightValue(nightTile);
	}

	[[nodiscard]] QByteArray tonsiteStorageToken() const override {
		return Core::App().settings().tonsiteStorageToken();
	}

	void setTonsiteStorageToken(const QByteArray &token) override {
		Core::App().settings().setTonsiteStorageToken(token);
	}

	void saveSettingsDelayed() override {
		Core::App().saveSettingsDelayed();
	}
};

} // namespace

std::unique_ptr<StorageSettingsCapabilities> CreateDesktopStorageSettingsCapabilities() {
	return std::make_unique<DesktopStorageSettingsCapabilities>();
}

} // namespace TgCli::Capabilities
