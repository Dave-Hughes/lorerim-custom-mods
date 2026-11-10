#include "Ledger.h"

namespace
{
	constexpr std::uint32_t kUniqueID = 'ZNTH';
	constexpr std::uint32_t kRecordEarned = 'EARN';
	constexpr std::uint32_t kRecordUnearned = 'UNRN';
	constexpr std::uint32_t kRecordMade = 'MADE';
	constexpr std::uint32_t kRecordKills = 'KILL';
	constexpr std::uint32_t kRecordVersion = 1;

	constexpr std::size_t kMaxKills = 2048;

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

void Ledger::Place(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count)
{
	std::scoped_lock lock(_lock);
	GiveEarned(a_holder, a_form, a_count);
}

std::int32_t Ledger::Transfer(RE::FormID a_from, RE::FormID a_to, RE::FormID a_form, std::int32_t a_count)
{
	if (a_count <= 0) {
		return 0;
	}
	std::scoped_lock lock(_lock);

	std::int32_t earned = 0;
	if (a_from == kPlayer) {
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

void Ledger::ClearLocked()
{
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

	logger::debug("saved: {} holders, {} not-yours items, {} made items, {} kills",
		self._earned.size(), self._unearned.size(), self._made.size(), self._killOrder.size());
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
		default:
			logger::warn("unknown record type {:08X}; skipping", type);
			break;
		}
	}

	logger::info("loaded: {} holders, {} not-yours items, {} made items, {} kills",
		self._earned.size(), self._unearned.size(), self._made.size(), self._killOrder.size());
}

void Ledger::OnRevert(SKSE::SerializationInterface*)
{
	Get().Clear();
}
