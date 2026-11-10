#include "Ledger.h"

namespace
{
	constexpr std::uint32_t kUniqueID = 'ZNTH';
	constexpr std::uint32_t kRecordEarned = 'EARN';
	constexpr std::uint32_t kRecordUnearned = 'UNRN';
	constexpr std::uint32_t kRecordMade = 'MADE';
	constexpr std::uint32_t kRecordKills = 'KILL';
	constexpr std::uint32_t kRecordViolations = 'VIOL';
	constexpr std::uint32_t kRecordMode = 'MODE';
	constexpr std::uint32_t kRecordRaceKills = 'KLRC';
	constexpr std::uint32_t kRecordConsole = 'CNSL';
	constexpr std::uint32_t kRecordOptions = 'OPTS';
	constexpr std::uint32_t kRecordPending = 'PEND';
	constexpr std::uint32_t kRecordVersion = 1;

	constexpr std::size_t kMaxKills = 2048;
	constexpr std::size_t kMaxViolations = 10000;

	// Maps a saved FormID onto the current load order. False if its plugin is gone.
	bool Resolve(SKSE::SerializationInterface* a_intfc, std::uint32_t a_old, RE::FormID& a_new)
	{
		a_new = 0;
		return a_intfc->ResolveFormID(a_old, a_new) && a_new != 0;
	}
}

std::string_view SourceVerb(Source a_source) noexcept
{
	switch (a_source) {
	case Source::kCrafted:
		return "crafted"sv;
	case Source::kCooked:
		return "cooked"sv;
	case Source::kBrewed:
		return "brewed"sv;
	case Source::kMined:
		return "mined"sv;
	case Source::kChopped:
		return "chopped"sv;
	case Source::kHunted:
		return "hunted"sv;
	default:
		return "made"sv;
	}
}

Ledger& Ledger::Get()
{
	static Ledger instance;
	return instance;
}

std::int32_t Ledger::TakeEarned(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count)
{
	if (a_count <= 0 || a_holder == 0) {
		return 0;
	}
	const auto holder = _earned.find(a_holder);
	if (holder == _earned.end()) {
		return 0;
	}
	const auto item = holder->second.find(a_form);
	if (item == holder->second.end()) {
		return 0;
	}
	const auto taken = std::min(a_count, item->second);
	item->second -= taken;
	if (item->second <= 0) {
		holder->second.erase(item);
		if (holder->second.empty()) {
			_earned.erase(holder);
		}
	}
	return taken;
}

void Ledger::GiveEarned(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count)
{
	if (a_count > 0 && a_holder != 0) {
		_earned[a_holder][a_form] += a_count;
	}
}

std::int32_t Ledger::TakeUnearned(RE::FormID a_form, std::int32_t a_count)
{
	if (a_count <= 0) {
		return 0;
	}
	const auto item = _unearned.find(a_form);
	if (item == _unearned.end()) {
		return 0;
	}
	const auto taken = std::min(a_count, item->second);
	item->second -= taken;
	if (item->second <= 0) {
		_unearned.erase(item);
	}
	return taken;
}

void Ledger::Mint(RE::FormID a_form, std::int32_t a_count, Source a_source)
{
	if (a_count <= 0 || a_source >= Source::kTotal) {
		return;
	}
	std::scoped_lock lock(_lock);
	GiveEarned(kPlayer, a_form, a_count);
	auto& made = _made.try_emplace(a_form, MadeCounts{}).first->second;
	made[static_cast<std::size_t>(a_source)] += static_cast<std::uint32_t>(a_count);
}

void Ledger::AddUnearned(RE::FormID a_form, std::int32_t a_count)
{
	if (a_count <= 0) {
		return;
	}
	std::scoped_lock lock(_lock);
	_unearned[a_form] += a_count;
}

void Ledger::AddEarned(RE::FormID a_form, std::int32_t a_count)
{
	std::scoped_lock lock(_lock);
	GiveEarned(kPlayer, a_form, a_count);
}

std::int32_t Ledger::Transfer(RE::FormID a_from, RE::FormID a_to, RE::FormID a_form, std::int32_t a_count, bool a_earnedFirst)
{
	if (a_count <= 0) {
		return 0;
	}
	std::scoped_lock lock(_lock);

	std::int32_t earned = 0;
	if (a_from == kPlayer && a_earnedFirst) {
		earned = TakeEarned(kPlayer, a_form, a_count);
		TakeUnearned(a_form, a_count - earned);
	} else if (a_from == kPlayer) {
		// Not-yours copies leave the pack first.
		const auto unearned = TakeUnearned(a_form, a_count);
		earned = TakeEarned(kPlayer, a_form, a_count - unearned);
	} else {
		// Earned copies come out of a container first.
		earned = TakeEarned(a_from, a_form, a_count);
	}

	if (a_to == kPlayer) {
		GiveEarned(kPlayer, a_form, earned);
		if (a_count > earned) {
			_unearned[a_form] += a_count - earned;
		}
	} else {
		GiveEarned(a_to, a_form, earned);
	}
	return earned;
}

std::int32_t Ledger::ReconcilePack(RE::FormID a_form, std::int32_t a_actual)
{
	std::scoped_lock lock(_lock);
	std::int32_t     earned = 0;
	if (const auto holder = _earned.find(kPlayer); holder != _earned.end()) {
		if (const auto item = holder->second.find(a_form); item != holder->second.end()) {
			earned = item->second;
		}
	}
	std::int32_t unearned = 0;
	if (const auto item = _unearned.find(a_form); item != _unearned.end()) {
		unearned = item->second;
	}
	const auto actual = std::max(0, a_actual);
	const auto tracked = earned + unearned;
	if (actual > tracked) {
		_unearned[a_form] = unearned + (actual - tracked);
		return actual - tracked;
	}
	if (actual < tracked) {
		auto       missing = tracked - actual;
		const auto fromUnearned = TakeUnearned(a_form, missing);
		missing -= fromUnearned;
		TakeEarned(kPlayer, a_form, missing);
		return -fromUnearned;
	}
	return 0;
}

std::int32_t Ledger::Consume(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count)
{
	if (a_count <= 0) {
		return 0;
	}
	std::scoped_lock lock(_lock);
	// Earned copies are used up first.
	const auto earned = TakeEarned(a_holder, a_form, a_count);
	if (a_holder == kPlayer) {
		TakeUnearned(a_form, a_count - earned);
	}
	return earned;
}

std::int32_t Ledger::Earned(RE::FormID a_holder, RE::FormID a_form) const
{
	std::scoped_lock lock(_lock);
	const auto holder = _earned.find(a_holder);
	if (holder == _earned.end()) {
		return 0;
	}
	const auto item = holder->second.find(a_form);
	return item == holder->second.end() ? 0 : item->second;
}

std::int32_t Ledger::UnearnedInPack(RE::FormID a_form) const
{
	std::scoped_lock lock(_lock);
	const auto item = _unearned.find(a_form);
	return item == _unearned.end() ? 0 : item->second;
}

bool Ledger::Tracks(RE::FormID a_holder) const
{
	std::scoped_lock lock(_lock);
	return _earned.contains(a_holder);
}

std::uint32_t Ledger::TotalMade(Source a_source) const
{
	if (a_source >= Source::kTotal) {
		return 0;
	}
	std::scoped_lock lock(_lock);
	std::uint32_t total = 0;
	for (const auto& [form, made] : _made) {
		total += made[static_cast<std::size_t>(a_source)];
	}
	return total;
}

std::uint32_t Ledger::TotalMade() const
{
	std::scoped_lock lock(_lock);
	std::uint32_t total = 0;
	for (const auto& [form, made] : _made) {
		for (const auto count : made) {
			total += count;
		}
	}
	return total;
}

void Ledger::NoteKill(RE::FormID a_actorRef)
{
	if (a_actorRef == 0) {
		return;
	}
	std::scoped_lock lock(_lock);
	if (!_kills.insert(a_actorRef).second) {
		return;
	}
	_killOrder.push_back(a_actorRef);
	while (_killOrder.size() > kMaxKills) {
		_kills.erase(_killOrder.front());
		_killOrder.pop_front();
	}
}

bool Ledger::WasKilledByPlayer(RE::FormID a_actorRef) const
{
	std::scoped_lock lock(_lock);
	return _kills.contains(a_actorRef);
}

std::uint32_t Ledger::Break(Rule a_rule, RE::FormID a_form, std::int32_t a_count, float a_day)
{
	std::scoped_lock lock(_lock);
	if (_violations.size() < kMaxViolations) {
		_violations.push_back({ a_rule, a_form, a_count, a_day });
	}
	return static_cast<std::uint32_t>(_violations.size());
}

std::uint32_t Ledger::ViolationCount() const
{
	std::scoped_lock lock(_lock);
	return static_cast<std::uint32_t>(_violations.size());
}

std::uint32_t Ledger::Options() const
{
	std::scoped_lock lock(_lock);
	return _options;
}

void Ledger::SetOptions(std::uint32_t a_options)
{
	std::scoped_lock lock(_lock);
	_options = a_options;
}

bool Ledger::ChoicePending() const
{
	std::scoped_lock lock(_lock);
	return _choicePending;
}

void Ledger::SetChoicePending(bool a_pending)
{
	std::scoped_lock lock(_lock);
	_choicePending = a_pending;
}

std::int32_t Ledger::Promote(RE::FormID a_form, std::int32_t a_count)
{
	std::scoped_lock lock(_lock);
	const auto       moved = TakeUnearned(a_form, a_count);
	GiveEarned(kPlayer, a_form, moved);
	return moved;
}

void Ledger::EarnPack(RE::FormID a_form, std::int32_t a_actual)
{
	std::scoped_lock lock(_lock);
	_unearned.erase(a_form);
	if (a_actual > 0) {
		_earned[kPlayer][a_form] = a_actual;
	}
}

void Ledger::TallyKill(RE::FormID a_race)
{
	if (a_race == 0) {
		return;
	}
	std::scoped_lock lock(_lock);
	++_killsByRace[a_race];
}

void Ledger::NoteConsole()
{
	std::scoped_lock lock(_lock);
	++_consoleOpened;
}

Ledger::Snapshot Ledger::TakeSnapshot() const
{
	std::scoped_lock lock(_lock);
	Snapshot         snapshot;
	snapshot.mode = _mode;
	snapshot.options = _options;
	snapshot.made.assign(_made.begin(), _made.end());
	snapshot.violations = _violations;
	snapshot.killsByRace.assign(_killsByRace.begin(), _killsByRace.end());
	snapshot.consoleOpened = _consoleOpened;
	return snapshot;
}

Mode Ledger::StoredMode() const
{
	std::scoped_lock lock(_lock);
	return _mode;
}

void Ledger::SetMode(Mode a_mode)
{
	std::scoped_lock lock(_lock);
	_mode = a_mode;
}

void Ledger::ClearLocked()
{
	_mode = Mode::kUnset;
	_killsByRace.clear();
	_consoleOpened = 0;
	_options = 0;
	_choicePending = false;
	_violations.clear();
	_earned.clear();
	_unearned.clear();
	_made.clear();
	_killOrder.clear();
	_kills.clear();
}

void Ledger::Clear()
{
	std::scoped_lock lock(_lock);
	ClearLocked();
}

void Ledger::RegisterSerialization()
{
	const auto intfc = SKSE::GetSerializationInterface();
	intfc->SetUniqueID(kUniqueID);
	intfc->SetSaveCallback(OnSave);
	intfc->SetLoadCallback(OnLoad);
	intfc->SetRevertCallback(OnRevert);
}

void Ledger::OnSave(SKSE::SerializationInterface* a_intfc)
{
	auto& self = Get();
	std::scoped_lock lock(self._lock);

	if (a_intfc->OpenRecord(kRecordEarned, kRecordVersion)) {
		std::uint32_t count = 0;
		for (const auto& [holder, items] : self._earned) {
			count += static_cast<std::uint32_t>(items.size());
		}
		a_intfc->WriteRecordData(count);
		for (const auto& [holder, items] : self._earned) {
			for (const auto& [form, copies] : items) {
				a_intfc->WriteRecordData(holder);
				a_intfc->WriteRecordData(form);
				a_intfc->WriteRecordData(copies);
			}
		}
	} else {
		logger::error("failed to open the earned record");
	}

	if (a_intfc->OpenRecord(kRecordUnearned, kRecordVersion)) {
		const auto count = static_cast<std::uint32_t>(self._unearned.size());
		a_intfc->WriteRecordData(count);
		for (const auto& [form, copies] : self._unearned) {
			a_intfc->WriteRecordData(form);
			a_intfc->WriteRecordData(copies);
		}
	} else {
		logger::error("failed to open the unearned record");
	}

	if (a_intfc->OpenRecord(kRecordMade, kRecordVersion)) {
		const auto count = static_cast<std::uint32_t>(self._made.size());
		const auto sources = static_cast<std::uint32_t>(Source::kTotal);
		a_intfc->WriteRecordData(count);
		a_intfc->WriteRecordData(sources);
		for (const auto& [form, made] : self._made) {
			a_intfc->WriteRecordData(form);
			for (const auto value : made) {
				a_intfc->WriteRecordData(value);
			}
		}
	} else {
		logger::error("failed to open the made record");
	}

	if (a_intfc->OpenRecord(kRecordKills, kRecordVersion)) {
		const auto count = static_cast<std::uint32_t>(self._killOrder.size());
		a_intfc->WriteRecordData(count);
		for (const auto ref : self._killOrder) {
			a_intfc->WriteRecordData(ref);
		}
	} else {
		logger::error("failed to open the kills record");
	}

	if (a_intfc->OpenRecord(kRecordViolations, kRecordVersion)) {
		const auto count = static_cast<std::uint32_t>(self._violations.size());
		a_intfc->WriteRecordData(count);
		for (const auto& violation : self._violations) {
			const auto rule = static_cast<std::uint8_t>(violation.rule);
			a_intfc->WriteRecordData(rule);
			a_intfc->WriteRecordData(violation.form);
			a_intfc->WriteRecordData(violation.count);
			a_intfc->WriteRecordData(violation.day);
		}
	} else {
		logger::error("failed to open the violations record");
	}

	if (a_intfc->OpenRecord(kRecordMode, kRecordVersion)) {
		const std::uint32_t mode = static_cast<std::uint32_t>(self._mode);
		a_intfc->WriteRecordData(mode);
	} else {
		logger::error("failed to open the mode record");
	}

	if (a_intfc->OpenRecord(kRecordOptions, kRecordVersion)) {
		a_intfc->WriteRecordData(self._options);
	}
	if (a_intfc->OpenRecord(kRecordPending, kRecordVersion)) {
		const std::uint32_t pending = self._choicePending ? 1 : 0;
		a_intfc->WriteRecordData(pending);
	}

	if (a_intfc->OpenRecord(kRecordConsole, kRecordVersion)) {
		a_intfc->WriteRecordData(self._consoleOpened);
	}

	if (a_intfc->OpenRecord(kRecordRaceKills, kRecordVersion)) {
		const auto count = static_cast<std::uint32_t>(self._killsByRace.size());
		a_intfc->WriteRecordData(count);
		for (const auto& [race, kills] : self._killsByRace) {
			a_intfc->WriteRecordData(race);
			a_intfc->WriteRecordData(kills);
		}
	}

	logger::debug("saved: {} holders, {} not-yours items, {} made items, {} kills, {} rules broken",
		self._earned.size(), self._unearned.size(), self._made.size(), self._killOrder.size(), self._violations.size());
}

void Ledger::OnLoad(SKSE::SerializationInterface* a_intfc)
{
	auto& self = Get();
	std::scoped_lock lock(self._lock);
	self.ClearLocked();

	std::uint32_t type = 0;
	std::uint32_t version = 0;
	std::uint32_t length = 0;
	while (a_intfc->GetNextRecordInfo(type, version, length)) {
		if (version != kRecordVersion) {
			logger::warn("unknown record version {} for {:08X}; skipping", version, type);
			continue;
		}

		std::uint32_t count = 0;
		if (!a_intfc->ReadRecordData(count)) {
			continue;
		}

		switch (type) {
		case kRecordMode:
			// The value read as the count is the mode itself.
			self._mode = count == static_cast<std::uint32_t>(Mode::kOath) ? Mode::kOath :
						 count == static_cast<std::uint32_t>(Mode::kOff) ? Mode::kOff : Mode::kUnset;
			break;
		case kRecordOptions:
			self._options = count;
			break;
		case kRecordPending:
			self._choicePending = count != 0;
			break;
		case kRecordConsole:
			// The value read as the count is the number itself.
			self._consoleOpened = count;
			break;
		case kRecordRaceKills:
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint32_t oldRace = 0;
				std::uint32_t kills = 0;
				if (!a_intfc->ReadRecordData(oldRace) || !a_intfc->ReadRecordData(kills)) {
					logger::warn("truncated race-kills record after {} of {} entries", i, count);
					break;
				}
				RE::FormID race = 0;
				if (Resolve(a_intfc, oldRace, race)) {
					self._killsByRace[race] += kills;
				}
			}
			break;
		case kRecordEarned:
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint32_t oldHolder = 0;
				std::uint32_t oldForm = 0;
				std::int32_t  copies = 0;
				if (!a_intfc->ReadRecordData(oldHolder) || !a_intfc->ReadRecordData(oldForm) || !a_intfc->ReadRecordData(copies)) {
					logger::warn("truncated earned record after {} of {} entries", i, count);
					break;
				}
				RE::FormID holder = 0;
				RE::FormID form = 0;
				if (copies > 0 && Resolve(a_intfc, oldHolder, holder) && Resolve(a_intfc, oldForm, form)) {
					self._earned[holder][form] += copies;
				}
			}
			break;
		case kRecordUnearned:
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint32_t oldForm = 0;
				std::int32_t  copies = 0;
				if (!a_intfc->ReadRecordData(oldForm) || !a_intfc->ReadRecordData(copies)) {
					logger::warn("truncated unearned record after {} of {} entries", i, count);
					break;
				}
				RE::FormID form = 0;
				if (copies > 0 && Resolve(a_intfc, oldForm, form)) {
					self._unearned[form] += copies;
				}
			}
			break;
		case kRecordMade:
			{
				std::uint32_t sources = 0;
				if (!a_intfc->ReadRecordData(sources)) {
					break;
				}
				for (std::uint32_t i = 0; i < count; ++i) {
					std::uint32_t oldForm = 0;
					if (!a_intfc->ReadRecordData(oldForm)) {
						logger::warn("truncated made record after {} of {} entries", i, count);
						break;
					}
					MadeCounts made{};
					bool       ok = true;
					for (std::uint32_t s = 0; s < sources; ++s) {
						std::uint32_t value = 0;
						if (!a_intfc->ReadRecordData(value)) {
							ok = false;
							break;
						}
						if (s < made.size()) {
							made[s] = value;
						}
					}
					if (!ok) {
						logger::warn("truncated made record inside entry {} of {}", i, count);
						break;
					}
					RE::FormID form = 0;
					if (Resolve(a_intfc, oldForm, form)) {
						self._made[form] = made;
					}
				}
			}
			break;
		case kRecordKills:
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint32_t oldRef = 0;
				if (!a_intfc->ReadRecordData(oldRef)) {
					logger::warn("truncated kills record after {} of {} entries", i, count);
					break;
				}
				RE::FormID ref = 0;
				if (Resolve(a_intfc, oldRef, ref) && self._kills.insert(ref).second) {
					self._killOrder.push_back(ref);
				}
			}
			break;
		case kRecordViolations:
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint8_t  rule = 0;
				std::uint32_t oldForm = 0;
				Violation     violation;
				if (!a_intfc->ReadRecordData(rule) || !a_intfc->ReadRecordData(oldForm) || !a_intfc->ReadRecordData(violation.count) ||
					!a_intfc->ReadRecordData(violation.day)) {
					logger::warn("truncated violations record after {} of {} entries", i, count);
					break;
				}
				violation.rule = rule < static_cast<std::uint8_t>(Rule::kTotal) ? static_cast<Rule>(rule) : Rule::kGear;
				// A broken rule stays on the record even if the item's plugin is gone.
				Resolve(a_intfc, oldForm, violation.form);
				self._violations.push_back(violation);
			}
			break;
		default:
			logger::warn("unknown record type {:08X}; skipping", type);
			break;
		}
	}

	logger::info("loaded: {} holders, {} not-yours items, {} made items, {} kills, {} rules broken",
		self._earned.size(), self._unearned.size(), self._made.size(), self._killOrder.size(), self._violations.size());
}

void Ledger::OnRevert(SKSE::SerializationInterface*)
{
	Get().Clear();
}
