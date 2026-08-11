/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/launcher.h"

#include "platform/platform_launcher.h"
#include "platform/platform_specific.h"
#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_file_utilities.h"
#include "ui/main_queue_processor.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "core/sandbox.h"
#include "core/version.h"
#include "base/concurrent_timer.h"
#include "base/options.h"
// TG_CHANGE_BEGIN: launcher-console-checkpoint-guard-include
#include "settings.h"
#include "../../tg_cli/hosted/hosted_console_checkpoint_guard.h"
#include "../../tg_cli/hosted/hosted_console_command_dispatcher.h"
#include "../../tg_cli/hosted/hosted_console_owner_probe.h"
// TG_CHANGE_END: launcher-console-checkpoint-guard-include

#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>
#include <QtCore/QLibraryInfo>
// TG_CHANGE_BEGIN: launcher-console-checkpoint-guard-include-cstdio
#include <cstdio>
// TG_CHANGE_END: launcher-console-checkpoint-guard-include-cstdio

namespace Core {
namespace {

uint64 InstallationTag = 0;

base::options::toggle OptionHighDpiDownscale({
	.id = kOptionHighDpiDownscale,
	.name = "High DPI downscale",
	.description = "Follow system interface scale settings exactly"
		" (another approach, likely better quality).",
	.scope = [] {
		return !Platform::IsMac()
			&& QLibraryInfo::version() >= QVersionNumber(6, 8);
	},
	.restartRequired = true,
});

base::options::toggle OptionFreeType({
	.id = kOptionFreeType,
	.name = "FreeType font engine",
	.description = "Use the font engine from Linux instead of the system one.",
	.scope = base::options::windows | base::options::macos,
	.restartRequired = true,
});

class FilteredCommandLineArguments {
public:
	FilteredCommandLineArguments(int argc, char **argv);

	int &count();
	char **values();

private:
	static constexpr auto kForwardArgumentCount = 1;

	int _count = 0;
	std::vector<QByteArray> _owned;
	std::vector<char*> _arguments;

	void pushArgument(const char *text);

};

FilteredCommandLineArguments::FilteredCommandLineArguments(
	int argc,
	char **argv) {
	// For now just pass only the first argument, the executable path.
	for (auto i = 0; i != kForwardArgumentCount; ++i) {
		pushArgument(argv[i]);
	}

#if defined Q_OS_WIN || defined Q_OS_MAC
	if (OptionFreeType.value() || OptionHighDpiDownscale.value()) {
		pushArgument("-platform");
#ifdef Q_OS_WIN
		pushArgument("windows:fontengine=freetype");
#else // Q_OS_WIN
		pushArgument("cocoa:fontengine=freetype");
#endif // !Q_OS_WIN
	}
#endif // Q_OS_WIN || Q_OS_MAC

	pushArgument(nullptr);
}

int &FilteredCommandLineArguments::count() {
	_count = _arguments.size() - 1;
	return _count;
}

char **FilteredCommandLineArguments::values() {
	return _arguments.data();
}

void FilteredCommandLineArguments::pushArgument(const char *text) {
	_owned.emplace_back(text);
	_arguments.push_back(_owned.back().data());
}

QString DebugModeSettingPath() {
	return cWorkingDir() + u"tdata/withdebug"_q;
}

void WriteDebugModeSetting() {
	auto file = QFile(DebugModeSettingPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(Logs::DebugEnabled() ? "1" : "0");
	}
}

void ComputeDebugMode() {
	Logs::SetDebugEnabled(cAlphaVersion() != 0);
	const auto debugModeSettingPath = DebugModeSettingPath();
	auto file = QFile(debugModeSettingPath);
	if (file.exists() && file.open(QIODevice::ReadOnly)) {
		Logs::SetDebugEnabled(file.read(1) != "0");
#if defined _DEBUG && !defined Q_OS_MAC
	} else {
		Logs::SetDebugEnabled(true);
#endif
	}
	if (cDebugMode()) {
		Logs::SetDebugEnabled(true);
	}
	if (Logs::DebugEnabled()) {
		QLoggingCategory::setFilterRules("qt.qpa.gl.debug=true");
	}
}

void ComputeExternalUpdater() {
	auto locations = QStandardPaths::standardLocations(
		QStandardPaths::AppDataLocation);
	if (locations.isEmpty()) {
		locations << QString();
	}
	locations[0] = QDir::cleanPath(cWorkingDir());
	locations << QDir::cleanPath(cExeDir());
	for (const auto &location : locations) {
		const auto dir = location + u"/externalupdater.d"_q;
		for (const auto &info : QDir(dir).entryInfoList(QDir::Files)) {
			QFile file(info.absoluteFilePath());
			if (file.open(QIODevice::ReadOnly)) {
				QTextStream fileStream(&file);
				while (!fileStream.atEnd()) {
					const auto path = fileStream.readLine();
					if (path == (cExeDir() + cExeName())) {
						SetUpdaterDisabledAtStartup();
						return;
					}
				}
			}
		}
	}
}

QString InstallBetaVersionsSettingPath() {
	return cWorkingDir() + u"tdata/devversion"_q;
}

void WriteInstallBetaVersionsSetting() {
	QFile f(InstallBetaVersionsSettingPath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write(cInstallBetaVersion() ? "1" : "0");
	}
}

void ComputeInstallBetaVersions() {
	const auto installBetaSettingPath = InstallBetaVersionsSettingPath();
	if (cAlphaVersion()) {
		cSetInstallBetaVersion(false);
	} else if (QFile::exists(installBetaSettingPath)) {
		QFile f(installBetaSettingPath);
		if (f.open(QIODevice::ReadOnly)) {
			cSetInstallBetaVersion(f.read(1) != "0");
		}
	} else if (AppBetaVersion) {
		WriteInstallBetaVersionsSetting();
	}
}

void ComputeInstallationTag() {
	InstallationTag = 0;
	auto file = QFile(cWorkingDir() + u"tdata/usertag"_q);
	if (file.open(QIODevice::ReadOnly)) {
		const auto result = file.read(
			reinterpret_cast<char*>(&InstallationTag),
			sizeof(uint64));
		if (result != sizeof(uint64)) {
			InstallationTag = 0;
		}
		file.close();
	}
	if (!InstallationTag) {
		auto generator = std::mt19937(std::random_device()());
		auto distribution = std::uniform_int_distribution<uint64>();
		do {
			InstallationTag = distribution(generator);
		} while (!InstallationTag);

		if (file.open(QIODevice::WriteOnly)) {
			file.write(
				reinterpret_cast<char*>(&InstallationTag),
				sizeof(uint64));
			file.close();
		}
	}
}

bool MoveLegacyAlphaFolder(const QString &folder, const QString &file) {
	const auto was = cExeDir() + folder;
	const auto now = cExeDir() + u"TelegramForcePortable"_q;
	if (QDir(was).exists() && !QDir(now).exists()) {
		const auto oldFile = was + "/tdata/" + file;
		const auto newFile = was + "/tdata/alpha";
		if (QFile::exists(oldFile) && !QFile::exists(newFile)) {
			if (!QFile(oldFile).copy(newFile)) {
				LOG(("FATAL: Could not copy '%1' to '%2'").arg(
					oldFile,
					newFile));
				return false;
			}
		}
		if (!QDir().rename(was, now)) {
			LOG(("FATAL: Could not rename '%1' to '%2'").arg(was, now));
			return false;
		}
	}
	return true;
}

bool MoveLegacyAlphaFolder() {
	if (!MoveLegacyAlphaFolder(u"TelegramAlpha_data"_q, u"alpha"_q)
		|| !MoveLegacyAlphaFolder(u"TelegramBeta_data"_q, u"beta"_q)) {
		return false;
	}
	return true;
}

bool CheckPortableVersionFolder() {
	if (!MoveLegacyAlphaFolder()) {
		return false;
	}

	const auto portable = cExeDir() + u"TelegramForcePortable"_q;
	QFile key(portable + u"/tdata/alpha"_q);
	if (cAlphaVersion()) {
		Assert(*AlphaPrivateKey != 0);

		cForceWorkingDir(portable);
		QDir().mkpath(cWorkingDir() + u"tdata"_q);
		cSetAlphaPrivateKey(QByteArray(AlphaPrivateKey));
		if (!key.open(QIODevice::WriteOnly)) {
			LOG(("FATAL: Could not open '%1' for writing private key!"
				).arg(key.fileName()));
			return false;
		}
		QDataStream dataStream(&key);
		dataStream.setVersion(QDataStream::Qt_5_3);
		dataStream << quint64(cRealAlphaVersion()) << cAlphaPrivateKey();
		return true;
	}
	if (!QDir(portable).exists()) {
		return true;
	}
	cForceWorkingDir(portable);
	if (!key.exists()) {
		return true;
	}

	if (!key.open(QIODevice::ReadOnly)) {
		LOG(("FATAL: could not open '%1' for reading private key. "
			"Delete it or reinstall private alpha version."
			).arg(key.fileName()));
		return false;
	}
	QDataStream dataStream(&key);
	dataStream.setVersion(QDataStream::Qt_5_3);

	quint64 v;
	QByteArray k;
	dataStream >> v >> k;
	if (dataStream.status() != QDataStream::Ok || k.isEmpty()) {
		LOG(("FATAL: '%1' is corrupted. "
			"Delete it or reinstall private alpha version."
			).arg(key.fileName()));
		return false;
	}
	cSetAlphaVersion(AppVersion * 1000ULL);
	cSetAlphaPrivateKey(k);
	cSetRealAlphaVersion(v);
	return true;
}

base::options::toggle OptionFractionalScalingEnabled({
	.id = kOptionFractionalScalingEnabled,
	.name = "Enable precise High DPI scaling",
	.description = "Follow system interface scale settings exactly.",
	.scope = base::options::windows | base::options::linux,
	.restartRequired = true,
});

} // namespace

const char kOptionFractionalScalingEnabled[] = "fractional-scaling-enabled";
const char kOptionHighDpiDownscale[] = "high-dpi-downscale";
const char kOptionFreeType[] = "freetype";

Launcher *Launcher::InstanceSetter::Instance = nullptr;

std::unique_ptr<Launcher> Launcher::Create(int argc, char *argv[]) {
	return std::make_unique<Platform::Launcher>(argc, argv);
}

Launcher::Launcher(int argc, char *argv[])
: _argc(argc)
, _argv(argv)
, _arguments(readArguments(_argc, _argv))
, _baseIntegration(_argc, _argv)
, _initialWorkingDir(QDir::currentPath() + '/') {
	crl::toggle_fp_exceptions(true);

	base::Integration::Set(&_baseIntegration);
}

Launcher::~Launcher() {
	InstanceSetter::Instance = nullptr;
}

void Launcher::init() {
	prepareSettings();
	initQtMessageLogging();

	QApplication::setApplicationName(u"TelegramDesktop"_q);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	// fallback session management is useless for tdesktop since it doesn't have
	// any "are you sure you want to close this window?" dialogs
	// but it produces bugs like https://github.com/telegramdesktop/tdesktop/issues/5022
	// and https://github.com/telegramdesktop/tdesktop/issues/7549
	// and https://github.com/telegramdesktop/tdesktop/issues/948
	// more info: https://doc.qt.io/qt-5/qguiapplication.html#isFallbackSessionManagementEnabled
	QApplication::setFallbackSessionManagementEnabled(false);
#endif // Qt < 6.0.0

	initHook();
}

void Launcher::initHighDpi() {
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
	qputenv("QT_DPI_ADJUSTMENT_POLICY", "AdjustDpi");
#endif // Qt < 6.2.0

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif // Qt < 6.0.0

	if (OptionHighDpiDownscale.value()) {
		qputenv("QT_WIDGETS_HIGHDPI_DOWNSCALE", "1");
	} else {
		qunsetenv("QT_WIDGETS_HIGHDPI_DOWNSCALE");
	}

	if (OptionFractionalScalingEnabled.value()
			|| OptionHighDpiDownscale.value()) {
		QApplication::setHighDpiScaleFactorRoundingPolicy(
			Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	} else {
		QApplication::setHighDpiScaleFactorRoundingPolicy(
			Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
	}
}

int Launcher::exec() {
	// TG_CHANGE_BEGIN: launcher-console-gates-init-order
	// Arguments must be parsed before any console gate is evaluated.
	init();
	if ((cConsoleAccountsMode() || cConsoleChatsMode() || cConsoleReadMode()
		|| cConsoleSendMode()
		|| cConsoleReplMode())
		&& (cConsoleOwnerProbeMode() || cConsoleProfileSnapshotMode())) {
		fprintf(
			stderr,
			"FATAL: console account modes cannot be combined with another console probe mode\n");
		return 1;
	}
	// TG_CHANGE_END: launcher-console-gates-init-order
	// TG_CHANGE_BEGIN: launcher-console-owner-probe-gates
	if (cConsoleOwnerProbeMode()) {
		if (!customWorkingDir()) {
			printf("owner-probe-status:owner-ambiguous\n");
			return 1;
		}
		auto timeoutMs = 1000;
		auto timeoutParsed = false;
		const auto timeoutOverride = qEnvironmentVariableIntValue(
			"TG_CLI_OWNER_PROBE_TIMEOUT_MS",
			&timeoutParsed);
		if (timeoutParsed && timeoutOverride >= 0) {
			timeoutMs = timeoutOverride;
		}
		const auto probe = TgCli::Hosted::ProbeHostedConsoleOwnerForWorkdir(
			customWorkingDirPath(),
			timeoutMs);
		printf(
			"owner-probe-status:%s\n",
			TgCli::Hosted::HostedConsoleOwnerProbeStatusToken(
				probe.status).toUtf8().constData());
		printf(
			"owner-probe-workdir-absolute:%s\n",
			probe.absoluteWorkdirPath.toUtf8().constData());
		printf(
			"owner-probe-local-server:%s\n",
			probe.localServerName.toUtf8().constData());
		printf(
			"owner-probe-timeout-ms:%d\n",
			probe.timeoutMs);
		printf(
			"owner-probe-elapsed-ms:%d\n",
			probe.elapsedMs);
		printf(
			"owner-probe-socket-state:%s\n",
			probe.socketStateToken.toUtf8().constData());
		printf(
			"owner-probe-socket-error:%s\n",
			probe.socketErrorToken.toUtf8().constData());
		printf(
			"owner-probe-socket-error-string:%s\n",
			probe.socketErrorString.toUtf8().constData());
		printf(
			"owner-probe-detail:%s\n",
			probe.detail.toUtf8().constData());
		return (probe.status == TgCli::Hosted::HostedConsoleOwnerProbeStatus::OwnerAmbiguous)
			? 1
			: 0;
	}
	// TG_CHANGE_END: launcher-console-owner-probe-gates
	// TG_CHANGE_BEGIN: launcher-console-profile-snapshot-gates-validate
	if (cConsoleProfileSnapshotMode()) {
		if (!customWorkingDir()) {
			fprintf(
				stderr,
				"FATAL: profile status mode requires explicit -workdir\n");
			return 1;
		}
		if (cConsoleLogPath().isEmpty()) {
			fprintf(
				stderr,
				"FATAL: profile status mode requires explicit -console-log\n");
			return 1;
		}
		const auto workdir = QDir(customWorkingDirPath()).canonicalPath();
		if (workdir.isEmpty()) {
			fprintf(
				stderr,
				"FATAL: profile status workdir does not exist: %s\n",
				customWorkingDirPath().toUtf8().constData());
			return 1;
		}
		const auto logPath = QDir(cConsoleLogPath()).absolutePath();
		if (logPath.startsWith(workdir + '/', Qt::CaseInsensitive)) {
			fprintf(
				stderr,
				"FATAL: -console-log must be outside the profile workdir\n");
			return 1;
		}
		_customWorkingDir = workdir + '/';
		gConsoleLogPath = logPath;
	}
	// TG_CHANGE_END: launcher-console-profile-snapshot-gates-validate
	// TG_CHANGE_BEGIN: launcher-console-accounts-gates
	if (cConsoleAccountsMode() || cConsoleChatsMode() || cConsoleReadMode()
		|| cConsoleSendMode()
		|| cConsoleReplMode()
		|| cConsoleCommandMode()) {
		if (cConsoleProfileSnapshotMode()) {
			fprintf(
				stderr,
				"FATAL: console account modes cannot be combined with -console-profile-snapshot\n");
			return 1;
		}
		if (cConsoleOwnerProbeMode()) {
			fprintf(
				stderr,
				"FATAL: console account modes cannot be combined with -console-owner-probe\n");
			return 1;
		}
		if (!customWorkingDir()) {
			fprintf(
				stderr,
				"FATAL: chats, read, send, and accounts modes require explicit -workdir\n");
			return 1;
		}
		const auto workdir = QDir(customWorkingDirPath()).canonicalPath();
		if (workdir.isEmpty()) {
			fprintf(
				stderr,
				"FATAL: chats, read, send, and accounts modes workdir does not exist: %s\n",
				customWorkingDirPath().toUtf8().constData());
			return 1;
		}
		if (cConsoleLogPath().isEmpty()) {
			fprintf(
				stderr,
				"FATAL: chats, read, send, and accounts modes require explicit -console-log\n");
			return 1;
		}
		const auto logPath = QDir(cConsoleLogPath()).absolutePath();
		if (logPath.startsWith(workdir + '/', Qt::CaseInsensitive)) {
			fprintf(
				stderr,
				"FATAL: -console-log must be outside the profile workdir\n");
			return 1;
		}
		_customWorkingDir = workdir + '/';
		gConsoleLogPath = logPath;
		const auto format = cConsoleFormat().trimmed().toLower();
		if (!format.isEmpty()
			&& format != QStringLiteral("text")
			&& format != QStringLiteral("json")) {
			fprintf(
				stderr,
				"FATAL: chats, read, and accounts modes support only -console-format text|json\n");
			return 1;
		}
		if (cConsoleChatsMode()
			&& (cConsoleChatsLimit() < 1 || cConsoleChatsLimit() > 1000)) {
			fprintf(
				stderr,
				"FATAL: -console-chats-limit must be an integer in range 1..1000\n");
			return 1;
		}
		if (cConsoleReadMode()
			&& (cConsoleReadLimit() < 1 || cConsoleReadLimit() > 100)) {
			fprintf(
				stderr,
				"FATAL: -console-read-limit must be an integer in range 1..100\n");
			return 1;
		}
		if (cConsoleSendMode()
			&& (cConsoleReadPeerId().isEmpty() || cConsoleSendText().isEmpty())) {
			fprintf(
				stderr,
				"FATAL: -console-send requires <stable-chat-id> <text>\n");
			return 1;
		}
	}

	// TG_CHANGE_END: launcher-console-accounts-gates
	// TG_CHANGE_BEGIN: launcher-console-checkpoint-guard-enforce
	if (cConsoleMode() && !cConsoleProfileSnapshotMode() && !cConsoleAccountsMode() && !cConsoleChatsMode() && !cConsoleReadMode() && !cConsoleSendMode() && !cConsoleReplMode() && !cConsoleCommandMode()) {
		const auto guard = TgCli::Hosted::EnforceHostedConsoleCheckpointGuard(
			customWorkingDir(),
			customWorkingDirPath());
		if (!guard.ok) {
			fprintf(
				stderr,
				"FATAL: hosted console checkpoint rejected: %s\n",
				guard.detail.toUtf8().constData());
			return 1;
		}
		if (!guard.canonicalWorkdirPath.isEmpty()) {
			_customWorkingDir = guard.canonicalWorkdirPath;
		}
	}

	// TG_CHANGE_END: launcher-console-checkpoint-guard-enforce
	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	}

	// Must be started before Platform is started.
	Logs::start();
	base::options::init(cWorkingDir() + "tdata/experimental_options.json");

	// Must be called after options are inited.
	initHighDpi();

	if (Logs::DebugEnabled()) {
		const auto openalLogPath = QDir::toNativeSeparators(
			cWorkingDir() + u"DebugLogs/last_openal_log.txt"_q);

		qputenv("ALSOFT_LOGLEVEL", "3");

#ifdef Q_OS_WIN
		_wputenv_s(
			L"ALSOFT_LOGFILE",
			openalLogPath.toStdWString().c_str());
#else // Q_OS_WIN
		qputenv(
			"ALSOFT_LOGFILE",
			QFile::encodeName(openalLogPath));
#endif // !Q_OS_WIN
	}

	// Must be started before Sandbox is created.
	Platform::start();
	ThirdParty::start();
	auto result = executeApplication();

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

	if (!UpdaterDisabled() && cRestartingUpdate()) {
		DEBUG_LOG(("Sandbox Info: executing updater to install update."));
		if (!launchUpdater(UpdaterLaunch::PerformUpdate)) {
			base::Platform::DeleteDirectory(cWorkingDir() + u"tupdates/temp"_q);
		}
	} else if (cRestarting()) {
		DEBUG_LOG(("Sandbox Info: executing Telegram because of restart."));
		launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	CrashReports::Finish();
	ThirdParty::finish();
	Platform::finish();
	Logs::finish();

	return result;
}

bool Launcher::validateCustomWorkingDir() {
	if (customWorkingDir()) {
		if (_customWorkingDir == cWorkingDir()) {
			_customWorkingDir = QString();
			return false;
		}
		cForceWorkingDir(_customWorkingDir);
		return true;
	}
	return false;
}

void Launcher::workingFolderReady() {
	srand((unsigned int)time(nullptr));

	ComputeDebugMode();
	ComputeExternalUpdater();
	ComputeInstallBetaVersions();
	ComputeInstallationTag();
}

void Launcher::writeDebugModeSetting() {
	WriteDebugModeSetting();
}

void Launcher::writeInstallBetaVersionsSetting() {
	WriteInstallBetaVersionsSetting();
}

bool Launcher::checkPortableVersionFolder() {
	return CheckPortableVersionFolder();
}

QStringList Launcher::readArguments(int argc, char *argv[]) const {
	Expects(argc >= 0);

	if (const auto native = readArgumentsHook(argc, argv)) {
		return *native;
	}

	auto result = QStringList();
	result.reserve(argc);
	for (auto i = 0; i != argc; ++i) {
		result.push_back(base::FromUtf8Safe(argv[i]));
	}
	return result;
}

const QStringList &Launcher::arguments() const {
	return _arguments;
}

QString Launcher::initialWorkingDir() const {
	return _initialWorkingDir;
}

bool Launcher::customWorkingDir() const {
	return !_customWorkingDir.isEmpty();
}
// TG_CHANGE_BEGIN: launcher-console-workdir-path-accessor
QString Launcher::customWorkingDirPath() const {
	return _customWorkingDir;
}
// TG_CHANGE_END: launcher-console-workdir-path-accessor

void Launcher::prepareSettings() {
	auto path = base::Platform::CurrentExecutablePath(_argc, _argv);
	LOG(("Executable path before check: %1").arg(path));
	if (cExeName().isEmpty()) {
		LOG(("WARNING: Could not compute executable path, some features will be disabled."));
	}

	processArguments();
}

void Launcher::initQtMessageLogging() {
	static QtMessageHandler OriginalMessageHandler = nullptr;
	OriginalMessageHandler = qInstallMessageHandler([](
			QtMsgType type,
			const QMessageLogContext &context,
			const QString &msg) {
		if (OriginalMessageHandler) {
			OriginalMessageHandler(type, context, msg);
		}
		if (Logs::DebugEnabled() || !Logs::started()) {
			if (!Logs::WritingEntry()) {
				// Sometimes Qt logs something inside our own logging.
				LOG((msg));
			}
		}
	});
}

uint64 Launcher::installationTag() const {
	return InstallationTag;
}

QByteArray Launcher::instanceHash() const {
	static const auto Result = [&] {
		QByteArray h(32, 0);
		if (customWorkingDir()) {
			const auto d = QFile::encodeName(
				QDir(cWorkingDir()).absolutePath());
			hashMd5Hex(d.constData(), d.size(), h.data());
		} else {
			const auto f = QFile::encodeName(cExeDir() + cExeName());
			hashMd5Hex(f.constData(), f.size(), h.data());
		}
		return h;
	}();
	return Result;
}

void Launcher::processArguments() {
	enum class KeyFormat {
		NoValues,
		OneValue,
		AllLeftValues,
	};
	auto parseMap = std::map<QByteArray, KeyFormat> {
		{ "-debug"          , KeyFormat::NoValues },
		{ "-testagent"      , KeyFormat::NoValues },
		{ "-key"            , KeyFormat::OneValue },
		{ "-autostart"      , KeyFormat::NoValues },
		{ "-fixprevious"    , KeyFormat::NoValues },
		{ "-cleanup"        , KeyFormat::NoValues },
		{ "-noupdate"       , KeyFormat::NoValues },
		{ "-tosettings"     , KeyFormat::NoValues },
		{ "-startintray"    , KeyFormat::NoValues },
		{ "-quit"           , KeyFormat::NoValues },
		{ "-workdir"        , KeyFormat::OneValue },
		// TG_CHANGE_BEGIN: launcher-console-argument-parse
		{ "-console"        , KeyFormat::NoValues },
		{ "-console-accounts" , KeyFormat::NoValues },
		{ "-console-chats" , KeyFormat::NoValues },
		{ "-console-chats-limit" , KeyFormat::OneValue },
		{ "-console-read" , KeyFormat::OneValue },
		{ "-console-read-limit" , KeyFormat::OneValue },
		{ "-console-read-cursor" , KeyFormat::OneValue },
		{ "-console-send" , KeyFormat::AllLeftValues },
		{ "-console-command" , KeyFormat::OneValue },
		{ "-console-repl" , KeyFormat::NoValues },
		{ "-console-account-index" , KeyFormat::OneValue },
		{ "-console-format" , KeyFormat::OneValue },
		{ "-console-owner-probe" , KeyFormat::NoValues },
		{ "-console-profile-snapshot" , KeyFormat::NoValues },
		{ "-console-exit"   , KeyFormat::NoValues },
		{ "-console-log"    , KeyFormat::OneValue },
		{ "-console-profile-snapshot-manifest" , KeyFormat::OneValue },
		// TG_CHANGE_END: launcher-console-argument-parse
		{ "--"              , KeyFormat::AllLeftValues },
		{ "-scale"          , KeyFormat::OneValue },
	};
	auto parseResult = QMap<QByteArray, QStringList>();
	auto parsingKey = QByteArray();
	auto parsingFormat = KeyFormat::NoValues;
	for (auto i = _arguments.cbegin(); i != _arguments.cend(); ++i) {
		if (i == _arguments.cbegin()) {
			continue;
		}
		const auto &argument = *i;
		switch (parsingFormat) {
		case KeyFormat::OneValue: {
			parseResult[parsingKey] = QStringList(argument.mid(0, 8192));
			parsingFormat = KeyFormat::NoValues;
		} break;
		case KeyFormat::AllLeftValues: {
			parseResult[parsingKey].push_back(argument.mid(0, 8192));
		} break;
		case KeyFormat::NoValues: {
			parsingKey = argument.toLatin1();
			auto it = parseMap.find(parsingKey);
			if (it != parseMap.end()) {
				parsingFormat = it->second;
				parseResult[parsingKey] = QStringList();
				continue;
			}
			parseResult["--"].push_back(argument.mid(0, 8192));
		} break;
		}
	}

	static const auto RegExp = QRegularExpression("[^a-z0-9\\-_]");
	gTestAgent = parseResult.contains("-testagent");
	gDebugMode = parseResult.contains("-debug") || gTestAgent;
	gKeyFile = parseResult
		.value("-key", {})
		.join(QString())
		.toLower()
		.replace(RegExp, {});
	gLaunchMode = parseResult.contains("-autostart") ? LaunchModeAutoStart
		: parseResult.contains("-fixprevious") ? LaunchModeFixPrevious
		: parseResult.contains("-cleanup") ? LaunchModeCleanup
		: LaunchModeNormal;
	gNoStartUpdate = parseResult.contains("-noupdate");
	gStartToSettings = parseResult.contains("-tosettings");
	gStartInTray = parseResult.contains("-startintray");
	gQuit = parseResult.contains("-quit");
	// TG_CHANGE_BEGIN: launcher-console-flag-assign
	gConsoleOwnerProbeMode = parseResult.contains("-console-owner-probe");
	gConsoleProfileSnapshotMode = parseResult.contains(
		"-console-profile-snapshot");
	gConsoleAccountsMode = parseResult.contains("-console-accounts");
	gConsoleChatsMode = parseResult.contains("-console-chats");
	gConsoleChatsLimit = 100;
	if (parseResult.contains("-console-chats-limit")) {
		auto ok = false;
		gConsoleChatsLimit = parseResult.value(
			"-console-chats-limit",
			{}).join(QString()).trimmed().toInt(&ok);
		if (!ok) {
			gConsoleChatsLimit = 0;
		}
	}
	gConsoleReadMode = parseResult.contains("-console-read");
	gConsoleReadPeerId = parseResult.value("-console-read", {}).join(QString());
	gConsoleReadCursor = parseResult.value(
		"-console-read-cursor",
		{}).join(QString());
	gConsoleSendMode = parseResult.contains("-console-send");
	if (gConsoleSendMode) {
		const auto sendArguments = parseResult.value("-console-send", {});
		gConsoleReadPeerId = sendArguments.value(0);
		gConsoleSendText = sendArguments.mid(1).join(QStringLiteral(" "));
	}
	gConsoleCommandMode = parseResult.contains("-console-command");
	gConsoleCommandArguments = parseResult.value("-console-command", {});
	gConsoleReplMode = parseResult.contains("-console-repl");
	if (gConsoleCommandMode) {
		gConsoleCommandArguments += parseResult.value("--", {});
	}
	gConsoleReadLimit = 20;
	if (parseResult.contains("-console-read-limit")) {
		auto ok = false;
		gConsoleReadLimit = parseResult.value(
			"-console-read-limit",
			{}).join(QString()).trimmed().toInt(&ok);
		if (!ok) {
			gConsoleReadLimit = 0;
		}
	}
	gConsoleMode = parseResult.contains("-console")
		|| gConsoleProfileSnapshotMode
		|| gConsoleAccountsMode
		|| gConsoleChatsMode
		|| gConsoleReadMode
		|| gConsoleSendMode
		|| gConsoleReplMode
		|| gConsoleCommandMode;
	gConsoleExitRequested = parseResult.contains("-console-exit");
	gConsoleLogPath = parseResult.value("-console-log", {}).join(QString());
	gConsoleAccountIndex = parseResult.value(
		"-console-account-index",
		{}).join(QString());
	gConsoleFormat = parseResult.value("-console-format", {}).join(QString());
	gConsoleProfileSnapshotManifestPath = parseResult.value(
		"-console-profile-snapshot-manifest",
		{}).join(QString());
	// TG_CHANGE_END: launcher-console-flag-assign
	_customWorkingDir = parseResult.value("-workdir", {}).join(QString());
	if (!_customWorkingDir.isEmpty()) {
		_customWorkingDir = QDir(_customWorkingDir).absolutePath() + '/';
	}

	const auto startUrls = parseResult.value("--", {});
	gStartUrls = startUrls | ranges::views::transform([&](const QString &url) {
		return QUrl::fromUserInput(url, _initialWorkingDir);
	}) | ranges::views::filter(&QUrl::isValid) | ranges::to<QList<QUrl>>;

	const auto scaleKey = parseResult.value("-scale", {});
	if (scaleKey.size() > 0) {
		using namespace style;
		const auto value = scaleKey[0].toInt();
		gConfigScale = ((value < kScaleMin) || (value > kScaleMax))
			? kScaleAuto
			: value;
	}
}

int Launcher::executeApplication() {
	FilteredCommandLineArguments arguments(_argc, _argv);
	Sandbox sandbox(arguments.count(), arguments.values());
	Ui::MainQueueProcessor processor;
	base::ConcurrentTimerEnvironment environment;
	return sandbox.start();
}

} // namespace Core
