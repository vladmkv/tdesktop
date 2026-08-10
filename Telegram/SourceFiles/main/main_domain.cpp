/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_domain.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/shortcuts.h"
#include "core/crash_reports.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "storage/storage_domain.h"
#include "storage/storage_account.h"
#include "storage/localstorage.h"
#include "export/export_settings.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
// TG_CHANGE_BEGIN: domain-capability-bundle-include
#include "../../tg_cli/capabilities/domain_lifecycle_capabilities.h"
// TG_CHANGE_END: domain-capability-bundle-include

namespace Main {

// TG_CHANGE_BEGIN: domain-capability-constructor-delegation
Domain::Domain(const QString &dataName)
: Domain(
	dataName,
	TgCli::Capabilities::CreateDesktopDomainCapabilityBundle()) {
}
// TG_CHANGE_END: domain-capability-constructor-delegation
// TG_CHANGE_BEGIN: domain-capability-constructor-overload
Domain::Domain(
		const QString &dataName,
		TgCli::Capabilities::DomainCapabilityBundle capabilityBundle)
: _dataName(dataName)
, _domainLifecycleCapabilities([&] {
	Expects(capabilityBundle.lifecycle != nullptr);
	return std::move(capabilityBundle.lifecycle);
}())
, _domainAccountFactoryCapabilities([&] {
	Expects(capabilityBundle.accountFactory != nullptr);
	return std::move(capabilityBundle.accountFactory);
}())
, _local(std::make_unique<Storage::Domain>(this, dataName)) {
	Expects(_domainLifecycleCapabilities != nullptr);
	Expects(_domainAccountFactoryCapabilities != nullptr);
// TG_CHANGE_END: domain-capability-constructor-overload
	_active.changes(
	) | rpl::take(1) | rpl::on_next([=] {
		// In case we had a legacy passcoded app we start settings here.
		// TG_CHANGE_BEGIN: domain-lifecycle-start-settings-background
		_domainLifecycleCapabilities->startSettingsAndBackground();
		// TG_CHANGE_END: domain-lifecycle-start-settings-background

		// TG_CHANGE_BEGIN: domain-lifecycle-notification-manager
		_domainLifecycleCapabilities->runOnMain(crl::guard(this, [=] {
			_domainLifecycleCapabilities->createNotificationsManager();
		}));
		// TG_CHANGE_END: domain-lifecycle-notification-manager
	}, _lifetime);

	_active.changes(
	) | rpl::map([](Main::Account *account) {
		return account ? account->sessionValue() : rpl::never<Session*>();
		}) | rpl::flatten_latest(
	) | rpl::map([](Main::Session *session) {
		return session
			? session->changes().peerFlagsValue(
				session->user(),
				Data::PeerUpdate::Flag::Username)
			: rpl::never<Data::PeerUpdate>();
	}) | rpl::flatten_latest(
	) | rpl::on_next([](const Data::PeerUpdate &update) {
		CrashReports::SetAnnotation("Username", update.peer->username());
	}, _lifetime);
}

Domain::~Domain() = default;

bool Domain::started() const {
	return !_accounts.empty();
}

Storage::StartResult Domain::start(const QByteArray &passcode) {
	Expects(!started());

	const auto result = _local->start(passcode);
	if (result == Storage::StartResult::Success) {
		activateAfterStarting();
		// TG_CHANGE_BEGIN: domain-lifecycle-on-main-export-suggest
		_domainLifecycleCapabilities->runOnMain(crl::guard(this, [=] {
			suggestExportIfNeeded();
		}));
		// TG_CHANGE_END: domain-lifecycle-on-main-export-suggest
	} else {
		Assert(!started());
	}
	return result;
}

void Domain::finish() {
	_accountToActivate = -1;
	_active.reset(nullptr);
	base::take(_accounts);
}

void Domain::suggestExportIfNeeded() {
	Expects(started());

	for (const auto &[index, account] : _accounts) {
		if (const auto session = account->maybeSession()) {
			const auto settings = session->local().readExportSettings();
			if (const auto availableAt = settings.availableAt) {
				session->data().suggestStartExport(availableAt);
			}
		}
	}
}

void Domain::accountAddedInStorage(AccountWithIndex accountWithIndex) {
	Expects(accountWithIndex.account != nullptr);

	for (const auto &[index, _] : _accounts) {
		if (index == accountWithIndex.index) {
			Unexpected("Repeated account index.");
		}
	}
	_accounts.push_back(std::move(accountWithIndex));
}

void Domain::activateFromStorage(int index) {
	_accountToActivate = index;
}

int Domain::activeForStorage() const {
	return _accountToActivate;
}

// TG_CHANGE_BEGIN: domain-account-factory-implementation
std::unique_ptr<Main::Account> Domain::createAccountForStorage(int index) {
	auto bundle = _domainAccountFactoryCapabilities->createAccountCapabilityBundle();
	Expects(bundle.network != nullptr);
	Expects(bundle.storage != nullptr);
	Expects(bundle.session != nullptr);

	return std::make_unique<Main::Account>(
		this,
		_dataName,
		index,
		std::move(bundle.network),
		std::move(bundle.storage),
		std::move(bundle.session));
}
// TG_CHANGE_END: domain-account-factory-implementation
void Domain::resetWithForgottenPasscode() {
	if (_accounts.empty()) {
		_local->startFromScratch();
		activateAfterStarting();
	} else {
		for (const auto &[index, account] : _accounts) {
			account->logOut();
		}
	}
}

void Domain::activateAfterStarting() {
	Expects(started());

	auto toActivate = _accounts.front().account.get();
	for (const auto &[index, account] : _accounts) {
		if (index == _accountToActivate) {
			toActivate = account.get();
		}
		watchSession(account.get());
	}

	activate(toActivate);
	removePasscodeIfEmpty();
}

const std::vector<Domain::AccountWithIndex> &Domain::accounts() const {
	return _accounts;
}

std::vector<not_null<Account*>> Domain::orderedAccounts() const {
	// TG_CHANGE_BEGIN: domain-lifecycle-accounts-order
	const auto order = _domainLifecycleCapabilities->accountsOrder();
	// TG_CHANGE_END: domain-lifecycle-accounts-order
	auto accounts = ranges::views::all(
		_accounts
	) | ranges::views::transform([](const Domain::AccountWithIndex &a) {
		return not_null{ a.account.get() };
	}) | ranges::to_vector;
	ranges::stable_sort(accounts, [&](
			not_null<Account*> a,
			not_null<Account*> b) {
		const auto aIt = a->sessionExists()
			? ranges::find(order, a->session().uniqueId())
			: end(order);
		const auto bIt = b->sessionExists()
			? ranges::find(order, b->session().uniqueId())
			: end(order);
		return aIt < bIt;
	});
	return accounts;
}

rpl::producer<> Domain::accountsChanges() const {
	return _accountsChanges.events();
}

Account *Domain::maybeLastOrSomeAuthedAccount() {
	auto result = (Account*)nullptr;
	for (const auto &[index, account] : _accounts) {
		if (!account->sessionExists()) {
			continue;
		} else if (index == _lastActiveIndex) {
			return account.get();
		} else if (!result) {
			result = account.get();
		}
	}
	return result;
}

int Domain::accountsAuthedCount() const {
	auto result = 0;
	for (const auto &[index, account] : _accounts) {
		if (account->sessionExists()) {
			++result;
		}
	}
	return result;
}

rpl::producer<Account*> Domain::activeValue() const {
	return _active.value();
}

Account &Domain::active() const {
	Expects(!_accounts.empty());

	Ensures(_active.current() != nullptr);
	return *_active.current();
}

rpl::producer<not_null<Account*>> Domain::activeChanges() const {
	return _active.changes() | rpl::map([](Account *value) {
		return not_null{ value };
	});
}

rpl::producer<Session*> Domain::activeSessionChanges() const {
	return _activeSessions.events();
}

rpl::producer<Session*> Domain::activeSessionValue() const {
	const auto current = _accounts.empty()
		? nullptr
		: active().maybeSession();
	return rpl::single(current) | rpl::then(_activeSessions.events());
}

int Domain::unreadBadge() const {
	return _unreadBadge;
}

bool Domain::unreadBadgeMuted() const {
	return _unreadBadgeMuted;
}

rpl::producer<> Domain::unreadBadgeChanges() const {
	return _unreadBadgeChanges.events();
}

void Domain::notifyUnreadBadgeChanged() {
	for (const auto &[index, account] : _accounts) {
		if (const auto session = account->maybeSession()) {
			session->data().notifyUnreadBadgeChanged();
		}
	}
}

void Domain::updateUnreadBadge() {
	_unreadBadge = 0;
	_unreadBadgeMuted = true;
	for (const auto &[index, account] : _accounts) {
		if (const auto session = account->maybeSession()) {
			const auto data = &session->data();
			_unreadBadge += data->unreadBadge();
			if (!data->unreadBadgeMuted()) {
				_unreadBadgeMuted = false;
			}
		}
	}
	_unreadBadgeChanges.fire({});
}

void Domain::scheduleUpdateUnreadBadge() {
	if (_unreadBadgeUpdateScheduled || Core::Quitting()) {
		return;
	}
	_unreadBadgeUpdateScheduled = true;
	// TG_CHANGE_BEGIN: domain-lifecycle-postpone-unread-badge
	_domainLifecycleCapabilities->postponeCall(crl::guard(this, [=] {
		_unreadBadgeUpdateScheduled = false;
		updateUnreadBadge();
	}));
	// TG_CHANGE_END: domain-lifecycle-postpone-unread-badge
}

not_null<Main::Account*> Domain::add(MTP::Environment environment) {
	Expects(started());
	Expects(_accounts.size() < kPremiumMaxAccounts);

	static const auto cloneConfig = [](const MTP::Config &config) {
		return std::make_unique<MTP::Config>(config);
	};
	auto mainDcId = MTP::Instance::Fields::kNotSetMainDc;
	const auto accountConfig = [&](not_null<Account*> account) {
		mainDcId = account->mtp().mainDcId();
		return cloneConfig(account->mtp().config());
	};
	auto config = [&] {
		if (_active.current()->mtp().environment() == environment) {
			return accountConfig(_active.current());
		}
		for (const auto &[index, account] : _accounts) {
			if (account->mtp().environment() == environment) {
				return accountConfig(account.get());
			}
		}
		// TG_CHANGE_BEGIN: domain-lifecycle-fallback-config-copy
		if (environment == MTP::Environment::Production) {
			auto copy = _domainLifecycleCapabilities->fallbackProductionConfigCopy();
			Expects(copy != nullptr);
			return copy;
		}
		return std::make_unique<MTP::Config>(environment);
		// TG_CHANGE_END: domain-lifecycle-fallback-config-copy
	}();
	auto index = 0;
	while (ranges::contains(_accounts, index, &AccountWithIndex::index)) {
		++index;
	}
	// TG_CHANGE_BEGIN: domain-add-account-factory-usage
	_accounts.push_back(AccountWithIndex{
		.index = index,
		.account = createAccountForStorage(index)
	});
	// TG_CHANGE_END: domain-add-account-factory-usage
	const auto account = _accounts.back().account.get();
	account->setMtpMainDcId(mainDcId);
	_local->startAdded(account, std::move(config));
	watchSession(account);
	_accountsChanges.fire({});

	// TG_CHANGE_BEGIN: domain-lifecycle-main-menu-settings
	if (_accounts.size() == 2
		&& !_domainLifecycleCapabilities->mainMenuAccountsShown()) {
		_domainLifecycleCapabilities->setMainMenuAccountsShown(true);
		_domainLifecycleCapabilities->saveSettingsDelayed();
	}
	// TG_CHANGE_END: domain-lifecycle-main-menu-settings

	return account;
}

void Domain::addActivated(MTP::Environment environment, bool newWindow) {
	// TG_CHANGE_BEGIN: domain-lifecycle-window-selection
	const auto added = [&](not_null<Main::Account*> account) {
		if (newWindow) {
			_domainLifecycleCapabilities->ensureSeparateWindowFor(account);
		} else if (const auto window = _domainLifecycleCapabilities->separateWindowFor(
				account)) {
			window->activate();
		} else {
			activate(account);
		}
	};
	// TG_CHANGE_END: domain-lifecycle-window-selection
	if (accounts().size() < maxAccounts()) {
		added(add(environment));
	} else {
		for (auto &[index, account] : accounts()) {
			if (!account->sessionExists()
				&& account->mtp().environment() == environment) {
				added(account.get());
				break;
			}
		}
	}
}

void Domain::watchSession(not_null<Account*> account) {
	account->sessionValue(
	) | rpl::filter([=](Session *session) {
		return session != nullptr;
	}) | rpl::on_next([=](Session *session) {
		session->data().unreadBadgeChanges(
		) | rpl::on_next([=] {
			scheduleUpdateUnreadBadge();
		}, session->lifetime());

		Data::AmPremiumValue(
			session
		) | rpl::on_next([=] {
			_lastMaxAccounts = maxAccounts();
		}, session->lifetime());
	}, account->lifetime());

	account->sessionChanges(
	) | rpl::filter([=](Session *session) {
		return !session;
	}) | rpl::on_next([=] {
		scheduleUpdateUnreadBadge();
		closeAccountWindows(account);
		if (!Core::Quitting()) {
			// TG_CHANGE_BEGIN: domain-lifecycle-on-main-remove-redundant
			_domainLifecycleCapabilities->runOnMain(crl::guard(this, [=] {
				removeRedundantAccounts();
			}));
			// TG_CHANGE_END: domain-lifecycle-on-main-remove-redundant
		}
	}, account->lifetime());
}

void Domain::closeAccountWindows(not_null<Main::Account*> account) {
	auto another = (Main::Account*)nullptr;
	for (auto i = _accounts.begin(); i != _accounts.end(); ++i) {
		const auto other = not_null(i->account.get());
		if (other == account) {
			continue;
		// TG_CHANGE_BEGIN: domain-lifecycle-close-account-windows-separate-window
		} else if (_domainLifecycleCapabilities->separateWindowFor(other)) {
			const auto that = _domainLifecycleCapabilities->separateWindowFor(account);
			// TG_CHANGE_END: domain-lifecycle-close-account-windows-separate-window
			if (that) {
				that->close();
			}
		} else if (!another
			|| (other->sessionExists() && !another->sessionExists())) {
			another = other;
		}
	}
	if (another) {
		activate(another);
	}
}

bool Domain::removePasscodeIfEmpty() {
	if (_accounts.size() != 1 || _active.current()->sessionExists()) {
		return false;
	}
	Local::reset();

	// We completely logged out, remove the passcode if it was there.
	// TG_CHANGE_BEGIN: domain-lifecycle-passcode-transitions
	if (_domainLifecycleCapabilities->passcodeLocked()) {
		_domainLifecycleCapabilities->unlockPasscode();
	}
	if (!_local->hasLocalPasscode()) {
		return false;
	}
	_local->setPasscode(QByteArray());
	_domainLifecycleCapabilities->setSystemUnlockEnabled(false);
	_domainLifecycleCapabilities->saveSettingsDelayed();
	return true;
}

void Domain::removeRedundantAccounts() {
	Expects(started());

	const auto was = _accounts.size();
	for (auto i = _accounts.begin(); i != _accounts.end();) {
		if (_domainLifecycleCapabilities->keepAccountWithoutSession(i->account.get())
			|| _domainLifecycleCapabilities->separateWindowFor(not_null(i->account.get()))
			|| i->account->sessionExists()) {
			++i;
			continue;
		}
		checkForLastProductionConfig(i->account.get());
		i = _accounts.erase(i);
	}
	// TG_CHANGE_END: domain-lifecycle-passcode-transitions

	if (!removePasscodeIfEmpty() && _accounts.size() != was) {
		scheduleWriteAccounts();
		_accountsChanges.fire({});
	}
}

void Domain::checkForLastProductionConfig(
		not_null<Main::Account*> account) {
	const auto mtp = &account->mtp();
	if (mtp->environment() != MTP::Environment::Production) {
		return;
	}
	for (const auto &[index, other] : _accounts) {
		if (other.get() != account
			&& other->mtp().environment() == MTP::Environment::Production) {
			return;
		}
	}
	// TG_CHANGE_BEGIN: domain-lifecycle-refresh-fallback-config
	_domainLifecycleCapabilities->refreshFallbackProductionConfig(mtp->config());
	// TG_CHANGE_END: domain-lifecycle-refresh-fallback-config
}

void Domain::maybeActivate(not_null<Main::Account*> account) {
	// TG_CHANGE_BEGIN: domain-lifecycle-prevent-or-invoke
	if (_domainLifecycleCapabilities->separateWindowFor(account)) {
		activate(account);
	} else {
		_domainLifecycleCapabilities->preventOrInvoke(crl::guard(account, [=] {
			activate(account);
		}));
	}
	// TG_CHANGE_END: domain-lifecycle-prevent-or-invoke
}

void Domain::activate(not_null<Main::Account*> account) {
	// TG_CHANGE_BEGIN: domain-lifecycle-activate-window
	if (const auto window = _domainLifecycleCapabilities->separateWindowFor(account)) {
		window->activate();
	}
	// TG_CHANGE_END: domain-lifecycle-activate-window
	if (_active.current() == account.get()) {
		return;
	}
	const auto i = ranges::find(_accounts, account.get(), [](
			const AccountWithIndex &value) {
		return value.account.get();
	});
	Assert(i != end(_accounts));
	const auto changed = (_accountToActivate != i->index);
	auto wasAuthed = false;

	_activeLifetime.destroy();
	if (_active.current()) {
		_lastActiveIndex = _accountToActivate;
		wasAuthed = _active.current()->sessionExists();
	}
	_accountToActivate = i->index;
	_active = account.get();
	_active.current()->sessionValue(
	) | rpl::start_to_stream(_activeSessions, _activeLifetime);

	if (changed) {
		if (wasAuthed) {
			scheduleWriteAccounts();
		} else {
			// TG_CHANGE_BEGIN: domain-lifecycle-on-main-remove-redundant-after-activate
			_domainLifecycleCapabilities->runOnMain(crl::guard(this, [=] {
				removeRedundantAccounts();
			}));
			// TG_CHANGE_END: domain-lifecycle-on-main-remove-redundant-after-activate
		}
	}
}

void Domain::scheduleWriteAccounts() {
	if (_writeAccountsScheduled) {
		return;
	}
	_writeAccountsScheduled = true;
	// TG_CHANGE_BEGIN: domain-lifecycle-on-main-write-accounts
	_domainLifecycleCapabilities->runOnMain(crl::guard(this, [=] {
		_writeAccountsScheduled = false;
		_local->writeAccounts();
	}));
	// TG_CHANGE_END: domain-lifecycle-on-main-write-accounts
}

int Domain::maxAccounts() const {
	const auto premiumCount = ranges::count_if(accounts(), [](
			const Main::Domain::AccountWithIndex &d) {
		return d.account->sessionExists()
			&& (d.account->session().premium()
				|| d.account->session().isTestMode());
	});
	return std::min(int(premiumCount) + kMaxAccounts, kPremiumMaxAccounts);
}

rpl::producer<int> Domain::maxAccountsChanges() const {
	return _lastMaxAccounts.changes();
}

} // namespace Main
